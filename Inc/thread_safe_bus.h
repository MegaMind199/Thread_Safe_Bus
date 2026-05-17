#pragma once

#ifndef THREAD_SAFE_BUS_H
#define THREAD_SAFE_BUS_H

#ifdef __cplusplus
extern "C" {
#endif


// TODO add client function time counting. if time exceeds client should be removed from bus.
// TODO add priority queue user should say which bus is priority to send data at first

#include "status.h"
#include <stdint.h>

#define TSB_FREE_ID 255u  // reserved ID, cannot be used by bus/client

typedef void (*sleep_fn_t)(void *ctx, uint32_t ms);
typedef int  (*get_time_ms_fn_t)(void *ctx);
typedef void (*mutex_lock_fn_t)(void *ctx);
typedef void (*mutex_unlock_fn_t)(void *ctx);


typedef struct{

    void *mutex_ctx;

    sleep_fn_t          sleep;
    get_time_ms_fn_t    get_time_ms;
    mutex_lock_fn_t     lock;
    mutex_unlock_fn_t   unlock;

}tsb_platform_api_t;

typedef struct{
    uint8_t  bus_id;
    uint8_t  owner_client_id;
    uint16_t memory_size;
    uint8_t  *memory;

}tsb_memory_t;

/*
 * Callback execution rule:
 * - Callbacks are executed while the internal TSB mutex is locked.
 * - Therefore callbacks must be short.
 * - Callbacks must not call any TSB API function.
 * - Callbacks must not block for a long time.
 *
 * If a callback calls tsb_send_data(), tsb_subscribe_bus(),
 * tsb_register_client(), tsb_register_bus(), or tsb_deinit(),
 * the system may deadlock when using a non-recursive mutex.
 */
typedef void (*client_fn_t)(const tsb_memory_t* memory);

typedef struct{
    uint8_t busy;

    uint8_t bus_id;
    uint8_t owner_client_id;
    uint8_t max_num_of_clients;
    
    uint8_t *client_ids;

    uint16_t memory_capacity;
    tsb_memory_t memory;

}tsb_bus_t;

typedef struct{
    uint8_t     client_id;
    client_fn_t client_callback;

}tsb_client_t;

typedef struct{
    tsb_platform_api_t  platform_api;

    uint8_t bus_num;
    uint8_t client_num;

}tsb_base_init_t;

typedef struct{
    tsb_platform_api_t  platform_api;

    uint8_t     bus_num;
    uint8_t     client_num;
    tsb_bus_t    *buses;
    tsb_client_t *clients;

    uint8_t initialized;

}tsb_base_t;

status_e tsb_deinit(tsb_base_t *base);
status_e tsb_init(tsb_base_t *base, const tsb_base_init_t *init);
status_e tsb_register_bus(tsb_base_t *base, const tsb_bus_t *bus_info);
status_e tsb_register_client(tsb_base_t *base, const tsb_client_t *client_info);
status_e tsb_subscribe_bus(tsb_base_t *base, uint8_t bus_id, uint8_t client_id);

status_e tsb_send_data(tsb_base_t *base, uint8_t client_id, uint8_t bus_id, const tsb_memory_t *memory);
status_e tsb_thread(tsb_base_t *base);



#ifdef __cplusplus
}
#endif

#endif // THREAD_SAFE_BUS_H
