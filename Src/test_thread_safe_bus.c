#include "thread_safe_bus.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

#define TEST_START(name) do { printf("[ RUN  ] %s\n", name); fflush(stdout); } while (0)
#define TEST_PASS(name)  do { printf("[ PASS ] %s\n\n", name); fflush(stdout); } while (0)
#define TEST_STEP(msg)   do { printf("        - %s\n", msg); fflush(stdout); } while (0)

/* ---------------- Fake platform layer ---------------- */

typedef struct {
    int locked;
    int lock_count;
    int unlock_count;
    int lock_errors;
    uint32_t now_ms;
} fake_mutex_t;

static fake_mutex_t g_mutex;

static void fake_sleep(void *ctx, uint32_t ms)
{
    fake_mutex_t *m = (fake_mutex_t *)ctx;
    m->now_ms += ms;
}

static int fake_get_time_ms(void *ctx)
{
    fake_mutex_t *m = (fake_mutex_t *)ctx;
    return (int)m->now_ms;
}

static void fake_lock(void *ctx)
{
    fake_mutex_t *m = (fake_mutex_t *)ctx;
    if (m->locked) {
        m->lock_errors++;
    }
    m->locked = 1;
    m->lock_count++;
}

static void fake_unlock(void *ctx)
{
    fake_mutex_t *m = (fake_mutex_t *)ctx;
    if (!m->locked) {
        m->lock_errors++;
    }
    m->locked = 0;
    m->unlock_count++;
}

static tsb_platform_api_t make_platform(void)
{
    memset(&g_mutex, 0, sizeof(g_mutex));

    tsb_platform_api_t api;
    api.mutex_ctx = &g_mutex;
    api.sleep = fake_sleep;
    api.get_time_ms = fake_get_time_ms;
    api.lock = fake_lock;
    api.unlock = fake_unlock;
    return api;
}

/* ---------------- Callback recorder ---------------- */

typedef struct {
    int call_count;
    uint8_t last_bus_id;
    uint8_t last_owner_client_id;
    uint16_t last_size;
    uint8_t last_data[32];
    int callback_was_called_while_locked;
} callback_record_t;

static callback_record_t g_cb_a;
static callback_record_t g_cb_b;
static callback_record_t g_cb_c;

static void reset_callback_records(void)
{
    memset(&g_cb_a, 0, sizeof(g_cb_a));
    memset(&g_cb_b, 0, sizeof(g_cb_b));
    memset(&g_cb_c, 0, sizeof(g_cb_c));
}

static void record_callback(callback_record_t *rec, const tsb_memory_t *memory)
{
    assert(memory != NULL);
    assert(memory->memory != NULL);
    assert(memory->memory_size <= sizeof(rec->last_data));

    rec->call_count++;
    rec->last_bus_id = memory->bus_id;
    rec->last_owner_client_id = memory->owner_client_id;
    rec->last_size = memory->memory_size;
    memcpy(rec->last_data, memory->memory, memory->memory_size);

    /* Your current implementation calls callbacks while the TSB mutex is locked. */
    rec->callback_was_called_while_locked = g_mutex.locked;
}

static void callback_a(const tsb_memory_t *memory)
{
    record_callback(&g_cb_a, memory);
}

static void callback_b(const tsb_memory_t *memory)
{
    record_callback(&g_cb_b, memory);
}

static void callback_c(const tsb_memory_t *memory)
{
    record_callback(&g_cb_c, memory);
}

/* ---------------- Test helpers ---------------- */

static void init_base(tsb_base_t *base, uint8_t bus_num, uint8_t client_num)
{
    tsb_base_init_t init;
    init.platform_api = make_platform();
    init.bus_num = bus_num;
    init.client_num = client_num;

    assert(tsb_init(base, &init) == STATUS_OK);
    assert(base->initialized == 1);
}

static void register_client(tsb_base_t *base, uint8_t id, client_fn_t cb)
{
    tsb_client_t client;
    client.client_id = id;
    client.client_callback = cb;

    assert(tsb_register_client(base, &client) == STATUS_OK);
}

static void register_bus(tsb_base_t *base,
                         uint8_t bus_id,
                         uint8_t owner_client_id,
                         uint8_t max_clients,
                         uint16_t capacity)
{
    tsb_bus_t bus;
    memset(&bus, 0, sizeof(bus));
    bus.bus_id = bus_id;
    bus.owner_client_id = owner_client_id;
    bus.max_num_of_clients = max_clients;
    bus.memory.memory_size = capacity;

    assert(tsb_register_bus(base, &bus) == STATUS_OK);
}

static void send_data(tsb_base_t *base,
                      uint8_t client_id,
                      uint8_t bus_id,
                      const uint8_t *data,
                      uint16_t size)
{
    tsb_memory_t memory;
    memory.bus_id = bus_id;
    memory.owner_client_id = client_id;
    memory.memory_size = size;
    memory.memory = (uint8_t *)data;

    assert(tsb_send_data(base, client_id, bus_id, &memory) == STATUS_OK);
}

/* ---------------- Tests ---------------- */

static void test_init_and_deinit(void)
{
    TEST_START("test_init_and_deinit");
    TEST_STEP("checking init allocates and deinit resets state");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));

    init_base(&base, 2, 3);

    assert(base.bus_num == 2);
    assert(base.client_num == 3);
    assert(base.buses != NULL);
    assert(base.clients != NULL);
    assert(g_mutex.lock_errors == 0);

    assert(tsb_deinit(&base) == STATUS_OK);
    assert(base.initialized == 0);
    assert(base.buses == NULL);
    assert(base.clients == NULL);
    assert(base.bus_num == 0);
    assert(base.client_num == 0);
    assert(g_mutex.lock_errors == 0);

    assert(tsb_deinit(&base) == STATUS_NOT_INITIALIZED);
    TEST_PASS("test_init_and_deinit");
}

static void test_invalid_init_arguments(void)
{
    TEST_START("test_invalid_init_arguments");
    TEST_STEP("checking NULL base/init and invalid platform callbacks");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));

    assert(tsb_init(NULL, NULL) == STATUS_INVALID_ARGUMENT);

    tsb_base_init_t init;
    memset(&init, 0, sizeof(init));
    init.bus_num = 1;
    init.client_num = 1;

    assert(tsb_init(&base, &init) == STATUS_INVALID_ARGUMENT);
    TEST_PASS("test_invalid_init_arguments");
}

static void test_register_client_errors(void)
{
    TEST_START("test_register_client_errors");
    TEST_STEP("checking NULL callback, duplicate client, full client table");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));
    init_base(&base, 1, 2);

    tsb_client_t invalid;
    invalid.client_id = 1;
    invalid.client_callback = NULL;
    assert(tsb_register_client(&base, &invalid) == STATUS_INVALID_ARGUMENT);

    tsb_client_t c1 = { .client_id = 1, .client_callback = callback_a };
    assert(tsb_register_client(&base, &c1) == STATUS_OK);
    assert(tsb_register_client(&base, &c1) == STATUS_ALREADY_EXISTS);

    tsb_client_t c2 = { .client_id = 2, .client_callback = callback_b };
    tsb_client_t c3 = { .client_id = 3, .client_callback = callback_c };
    assert(tsb_register_client(&base, &c2) == STATUS_OK);
    assert(tsb_register_client(&base, &c3) == STATUS_FULL);

    assert(tsb_deinit(&base) == STATUS_OK);
    TEST_PASS("test_register_client_errors");
}

static void test_register_bus_errors(void)
{
    TEST_START("test_register_bus_errors");
    TEST_STEP("checking invalid bus config, duplicate bus, full bus table");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));
    init_base(&base, 1, 2);
    register_client(&base, 10, callback_a);

    tsb_bus_t bus;
    memset(&bus, 0, sizeof(bus));
    bus.bus_id = 5;
    bus.owner_client_id = 99;  /* unknown owner */
    bus.max_num_of_clients = 1;
    bus.memory.memory_size = 8;
    assert(tsb_register_bus(&base, &bus) == STATUS_INVALID_ARGUMENT);

    bus.owner_client_id = 10;
    bus.max_num_of_clients = 0;
    assert(tsb_register_bus(&base, &bus) == STATUS_INVALID_ARGUMENT);

    bus.max_num_of_clients = 1;
    bus.memory.memory_size = 0;
    assert(tsb_register_bus(&base, &bus) == STATUS_INVALID_ARGUMENT);

    bus.memory.memory_size = 8;
    assert(tsb_register_bus(&base, &bus) == STATUS_OK);
    assert(tsb_register_bus(&base, &bus) == STATUS_ALREADY_EXISTS);

    tsb_bus_t bus2;
    memset(&bus2, 0, sizeof(bus2));
    bus2.bus_id = 6;
    bus2.owner_client_id = 10;
    bus2.max_num_of_clients = 1;
    bus2.memory.memory_size = 8;
    assert(tsb_register_bus(&base, &bus2) == STATUS_FULL);

    assert(tsb_deinit(&base) == STATUS_OK);
    TEST_PASS("test_register_bus_errors");
}

static void test_subscribe_errors(void)
{
    TEST_START("test_subscribe_errors");
    TEST_STEP("checking invalid bus/client, duplicate subscription, full subscription list");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));
    init_base(&base, 1, 3);

    register_client(&base, 1, callback_a);
    register_client(&base, 2, callback_b);
    register_client(&base, 3, callback_c);
    register_bus(&base, 7, 1, 1, 8);

    assert(tsb_subscribe_bus(&base, 99, 2) == STATUS_INVALID_ARGUMENT); /* bad bus */
    assert(tsb_subscribe_bus(&base, 7, 99) == STATUS_INVALID_ARGUMENT); /* bad client */

    assert(tsb_subscribe_bus(&base, 7, 2) == STATUS_OK);
    assert(tsb_subscribe_bus(&base, 7, 2) == STATUS_ALREADY_EXISTS);
    assert(tsb_subscribe_bus(&base, 7, 3) == STATUS_FULL);

    assert(tsb_deinit(&base) == STATUS_OK);
    TEST_PASS("test_subscribe_errors");
}

static void test_send_and_dispatch_one_subscriber(void)
{
    TEST_START("test_send_and_dispatch_one_subscriber");
    TEST_STEP("checking owner send, payload copy, and one callback dispatch");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));
    reset_callback_records();
    init_base(&base, 1, 2);

    register_client(&base, 1, callback_a); /* owner */
    register_client(&base, 2, callback_b); /* subscriber */
    register_bus(&base, 9, 1, 2, 16);
    assert(tsb_subscribe_bus(&base, 9, 2) == STATUS_OK);

    uint8_t payload[] = { 10, 20, 30, 40 };
    send_data(&base, 1, 9, payload, sizeof(payload));

    assert(g_cb_a.call_count == 0);
    assert(g_cb_b.call_count == 0);

    assert(tsb_thread(&base) == STATUS_OK);

    assert(g_cb_a.call_count == 0); /* owner was not subscribed */
    assert(g_cb_b.call_count == 1);
    assert(g_cb_b.last_bus_id == 9);
    assert(g_cb_b.last_owner_client_id == 1);
    assert(g_cb_b.last_size == sizeof(payload));
    assert(memcmp(g_cb_b.last_data, payload, sizeof(payload)) == 0);
    assert(g_cb_b.callback_was_called_while_locked == 1);
    assert(g_mutex.lock_errors == 0);

    assert(tsb_deinit(&base) == STATUS_OK);
    TEST_PASS("test_send_and_dispatch_one_subscriber");
}

static void test_send_to_multiple_subscribers(void)
{
    TEST_START("test_send_to_multiple_subscribers");
    TEST_STEP("checking one message is delivered to two subscribed clients");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));
    reset_callback_records();
    init_base(&base, 1, 3);

    register_client(&base, 1, callback_a); /* owner */
    register_client(&base, 2, callback_b);
    register_client(&base, 3, callback_c);
    register_bus(&base, 4, 1, 3, 16);

    assert(tsb_subscribe_bus(&base, 4, 1) == STATUS_OK); /* owner can also subscribe */
    assert(tsb_subscribe_bus(&base, 4, 2) == STATUS_OK);
    assert(tsb_subscribe_bus(&base, 4, 3) == STATUS_OK);

    uint8_t payload[] = { 1, 2, 3 };
    send_data(&base, 1, 4, payload, sizeof(payload));
    assert(tsb_thread(&base) == STATUS_OK);

    assert(g_cb_a.call_count == 1);
    assert(g_cb_b.call_count == 1);
    assert(g_cb_c.call_count == 1);
    assert(memcmp(g_cb_a.last_data, payload, sizeof(payload)) == 0);
    assert(memcmp(g_cb_b.last_data, payload, sizeof(payload)) == 0);
    assert(memcmp(g_cb_c.last_data, payload, sizeof(payload)) == 0);

    assert(tsb_deinit(&base) == STATUS_OK);
    TEST_PASS("test_send_to_multiple_subscribers");
}

static void test_send_errors(void)
{
    TEST_START("test_send_errors");
    TEST_STEP("checking permission denied, oversized packet, zero size, NULL data, and busy behavior");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));
    init_base(&base, 1, 2);

    register_client(&base, 1, callback_a); /* owner */
    register_client(&base, 2, callback_b); /* non-owner */
    register_bus(&base, 8, 1, 1, 4);
    assert(tsb_subscribe_bus(&base, 8, 2) == STATUS_OK);

    uint8_t data4[] = { 1, 2, 3, 4 };
    uint8_t data5[] = { 1, 2, 3, 4, 5 };

    tsb_memory_t mem;
    mem.bus_id = 8;
    mem.owner_client_id = 1;
    mem.memory = data4;
    mem.memory_size = sizeof(data4);

    assert(tsb_send_data(&base, 99, 8, &mem) == STATUS_INVALID_ARGUMENT); /* unknown sender */
    assert(tsb_send_data(&base, 1, 99, &mem) == STATUS_INVALID_ARGUMENT); /* unknown bus */
    assert(tsb_send_data(&base, 2, 8, &mem) == STATUS_PERMISSION_DENIED); /* not owner */

    mem.memory = data5;
    mem.memory_size = sizeof(data5);
    assert(tsb_send_data(&base, 1, 8, &mem) == STATUS_INVALID_ARGUMENT); /* too large */

    mem.memory = data4;
    mem.memory_size = 0;
    assert(tsb_send_data(&base, 1, 8, &mem) == STATUS_INVALID_ARGUMENT); /* zero size */

    mem.memory = NULL;
    mem.memory_size = sizeof(data4);
    assert(tsb_send_data(&base, 1, 8, &mem) == STATUS_INVALID_ARGUMENT); /* null data */

    mem.memory = data4;
    mem.memory_size = sizeof(data4);
    assert(tsb_send_data(&base, 1, 8, &mem) == STATUS_OK);
    assert(tsb_send_data(&base, 1, 8, &mem) == STATUS_BUSY); /* not dispatched yet */

    assert(tsb_thread(&base) == STATUS_OK);
    assert(tsb_send_data(&base, 1, 8, &mem) == STATUS_OK); /* can send again after dispatch */
    assert(tsb_thread(&base) == STATUS_OK);

    assert(tsb_deinit(&base) == STATUS_OK);
    TEST_PASS("test_send_errors");
}

static void test_not_initialized_errors(void)
{
    TEST_START("test_not_initialized_errors");
    TEST_STEP("checking all public APIs return STATUS_NOT_INITIALIZED before init");
    tsb_base_t base;
    memset(&base, 0, sizeof(base));

    tsb_client_t client = { .client_id = 1, .client_callback = callback_a };

    tsb_bus_t bus;
    memset(&bus, 0, sizeof(bus));
    bus.bus_id = 2;
    bus.owner_client_id = 1;
    bus.max_num_of_clients = 1;
    bus.memory.memory_size = 8;

    uint8_t payload[] = { 1 };
    tsb_memory_t memory = {
        .bus_id = 2,
        .owner_client_id = 1,
        .memory_size = sizeof(payload),
        .memory = payload
    };

    assert(tsb_deinit(&base) == STATUS_NOT_INITIALIZED);
    assert(tsb_register_client(&base, &client) == STATUS_NOT_INITIALIZED);
    assert(tsb_register_bus(&base, &bus) == STATUS_NOT_INITIALIZED);
    assert(tsb_subscribe_bus(&base, 2, 1) == STATUS_NOT_INITIALIZED);
    assert(tsb_send_data(&base, 1, 2, &memory) == STATUS_NOT_INITIALIZED);
    assert(tsb_thread(&base) == STATUS_NOT_INITIALIZED);
    TEST_PASS("test_not_initialized_errors");
}

int test_thread_safe_bus(void)
{
    printf("==============================\n");
    printf("Thread Safe Bus test suite - VERY VERBOSE VERSION 3\n");
    printf("==============================\n\n");
    fflush(stdout);

    test_invalid_init_arguments();
    test_not_initialized_errors();
    test_init_and_deinit();
    test_register_client_errors();
    test_register_bus_errors();
    test_subscribe_errors();
    test_send_and_dispatch_one_subscriber();
    test_send_to_multiple_subscribers();
    test_send_errors();

    printf("==============================\n");
    printf("All thread_safe_bus tests passed.\n");
    printf("==============================\n");
    fflush(stdout);
    return 0;
}

#ifdef TSB_TEST_STANDALONE
int main(void)
{
    return test_thread_safe_bus();
}
#endif
