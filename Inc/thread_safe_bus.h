#pragma once

#ifndef THREAD_SAFE_BUS_H
#define THREAD_SAFE_BUS_H

#ifdef __cplusplus
extern "C" {
#endif


// TODO add client function time counting. if time exceeds client should be removed from bus.

#include "status.h"
#include <stdint.h>

typedef void (*sleep_fn_t)(void *ctx);
typedef int  (*get_time_ms_fn_t)(void *ctx);
typedef void (*mutex_lock_fn_t)(void *ctx);
typedef void (*mutex_unlock_fn_t)(void *ctx);


typedef struct{

    void *mutex_ctx;

    sleep_fn_t          sleep;
    get_time_ms_fn_t    get_time;
    mutex_lock_fn_t     lock;
    mutex_unlock_fn_t   unlock;

}mutex_ctx_t;

typedef struct{
    uint8_t bus_id;
    uint8_t owner_client_id;
    uint8_t memory_size;
    uint8_t *memory;

}tsb_memory_t;

typedef void (*client_fn_t)(tsb_memory_t* memory);

typedef struct{
    uint8_t busy;

    int8_t  bus_id;
    uint8_t owner_client_id;
    uint8_t max_num_of_clients;
    
    int8_t  *client_ids;

    tsb_memory_t memory;

}tsb_bus_t;

typedef struct{
    int8_t      client_id;
    client_fn_t client_callback;

}tsb_client_t;

typedef struct{
    mutex_ctx_t  mutex_ctx;

    uint8_t bus_num;
    uint8_t client_num;

}tsb_base_init_t;

typedef struct{
    mutex_ctx_t  mutex_ctx;

    uint8_t     bus_num;
    uint8_t     client_num;
    tsb_bus_t    *buses;
    tsb_client_t *clients;

}tsb_base_t;

status_e tsb_init(tsb_base_t *base, tsb_base_init_t *init);
status_e tsb_register_bus(tsb_base_t *base, tsb_bus_t *bus_info);
status_e tsb_register_client(tsb_base_t *base, tsb_client_t *client_info);
status_e tsb_subscribe_bus(tsb_base_t *base, uint8_t bus_id, uint8_t client_id);

status_e tsb_send_data(tsb_base_t *base, uint8_t client_id, uint8_t bus_id, tsb_memory_t *memory);
status_e tsb_thread(tsb_base_t *base);



#ifdef __cplusplus
}
#endif

#endif // THREAD_SAFE_BUS_H
