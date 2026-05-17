#include "thread_safe_bus.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static uint8_t get_idx(const uint8_t *array, uint8_t size, uint8_t value){
    for (uint8_t i = 0; i < size; i++) {
        if(array[i] == value) return i;
    }
    return TSB_FREE_ID;
}

static uint8_t get_bus_idx(tsb_base_t *base, uint8_t bus_id){
    for (uint8_t i = 0; i < base->bus_num; i++) {
        if(base->buses[i].bus_id == bus_id) return i;
    }
    return TSB_FREE_ID;
}

static uint8_t get_client_idx(tsb_base_t *base, uint8_t client_id){
    for (uint8_t i = 0; i < base->client_num; i++) {
        if(base->clients[i].client_id == client_id) return i;
    }
    return TSB_FREE_ID;
}

status_e tsb_init(tsb_base_t *base, const tsb_base_init_t *init){
    
    if(base == NULL || init == NULL) return STATUS_INVALID_ARGUMENT;
    if(init->platform_api.get_time_ms == NULL || init->platform_api.lock == NULL || 
        init->platform_api.unlock == NULL || init->platform_api.sleep == NULL) return STATUS_INVALID_ARGUMENT;
    
    base->buses         = malloc(sizeof(tsb_bus_t)    * init->bus_num);
    base->clients       = malloc(sizeof(tsb_client_t) * init->client_num);

    if(base->buses == NULL || base->clients == NULL){
        free(base->buses);
        free(base->clients);
        base->buses         = NULL;
        base->clients       = NULL;
        base->bus_num       = 0;
        base->client_num    = 0;
        return STATUS_NULL_POINTER;
    }

    base->platform_api  = init->platform_api;
    base->bus_num       = init->bus_num;
    base->client_num    = init->client_num;


    for (uint8_t i = 0; i < init->bus_num; i++) {
        base->buses[i].busy                     = 0;
        base->buses[i].bus_id                   = TSB_FREE_ID;
        base->buses[i].client_ids               = NULL;
        base->buses[i].memory.memory            = NULL;
        base->buses[i].memory.memory_size       = 0;
        base->buses[i].memory.bus_id            = TSB_FREE_ID;
        base->buses[i].memory.owner_client_id   = TSB_FREE_ID;
        base->buses[i].memory_capacity          = 0;
        base->buses[i].owner_client_id          = TSB_FREE_ID;
        base->buses[i].max_num_of_clients       = 0;
    }

    for (uint8_t i = 0; i < init->client_num; i++) {
        base->clients[i].client_id = TSB_FREE_ID;
        base->clients[i].client_callback = NULL;
    }

    base->initialized = 1;

    return STATUS_OK;
}

status_e tsb_deinit(tsb_base_t *base){
    if (base == NULL) return STATUS_INVALID_ARGUMENT;
    if (!base->initialized) return STATUS_NOT_INITIALIZED;

    base->platform_api.lock(base->platform_api.mutex_ctx);

    for (uint8_t i = 0; i < base->bus_num; i++) {
        free(base->buses[i].client_ids);
        free(base->buses[i].memory.memory);
        base->buses[i].client_ids = NULL;
        base->buses[i].memory.memory = NULL;
    }

    free(base->buses);
    free(base->clients);

    base->buses = NULL;
    base->clients = NULL;
    base->bus_num = 0;
    base->client_num = 0;

    base->platform_api.unlock(base->platform_api.mutex_ctx);

    base->initialized = 0;

    return STATUS_OK;
}

static status_e tsb_check_bus_register(tsb_base_t *base, const tsb_bus_t *register_info, uint8_t *new_bus_idx){

    if(register_info->max_num_of_clients == 0) return STATUS_INVALID_ARGUMENT;
    if(register_info->memory.memory_size == 0) return STATUS_INVALID_ARGUMENT;

    uint8_t bus_idx = get_bus_idx(base, register_info->bus_id);
    if(bus_idx != TSB_FREE_ID) return STATUS_ALREADY_EXISTS; // Bus ID already exists

    bus_idx = get_bus_idx(base, TSB_FREE_ID);
    if (bus_idx == TSB_FREE_ID) return STATUS_FULL; // No available bus

    uint8_t client_idx = get_client_idx(base, register_info->owner_client_id);
    if (client_idx == TSB_FREE_ID) return STATUS_INVALID_ARGUMENT;
    
    *new_bus_idx = bus_idx;
    return STATUS_OK;
}

status_e tsb_register_bus(tsb_base_t *base, const tsb_bus_t *register_info){
    if(base == NULL || register_info == NULL || register_info->bus_id == TSB_FREE_ID) return STATUS_INVALID_ARGUMENT;
    if (!base->initialized) return STATUS_NOT_INITIALIZED;
    base->platform_api.lock(base->platform_api.mutex_ctx);

    uint8_t new_bus_idx;
    status_e ret = tsb_check_bus_register(base, register_info, &new_bus_idx);
    if (ret != STATUS_OK) {
        base->platform_api.unlock(base->platform_api.mutex_ctx);
        return ret;
    }

    tsb_bus_t* temp = &base->buses[new_bus_idx];
    temp->client_ids         = malloc(sizeof(uint8_t) * register_info->max_num_of_clients);
    temp->memory.memory      = malloc(sizeof(uint8_t) * register_info->memory.memory_size);

    if(temp->client_ids == NULL || temp->memory.memory == NULL){
        free(temp->client_ids);
        free(temp->memory.memory);
        temp->client_ids = NULL;
        temp->memory.memory = NULL;

        base->platform_api.unlock(base->platform_api.mutex_ctx);
        return STATUS_NULL_POINTER;
    }

    temp->busy               = 0;
    temp->bus_id             = register_info->bus_id;
    temp->owner_client_id    = register_info->owner_client_id;
    temp->max_num_of_clients = register_info->max_num_of_clients;

    temp->memory.memory_size = 0;
    temp->memory.bus_id      = register_info->bus_id;
    temp->memory_capacity    = register_info->memory.memory_size;

    memset(temp->client_ids, TSB_FREE_ID, sizeof(uint8_t) * register_info->max_num_of_clients);

    base->platform_api.unlock(base->platform_api.mutex_ctx);

    return STATUS_OK;   
}

static status_e tsb_check_client_register(tsb_base_t *base, const tsb_client_t *client_info, uint8_t *new_client_idx){
    
    if (client_info->client_callback == NULL) {
        return STATUS_INVALID_ARGUMENT;
    }
    
    uint8_t client_idx = get_client_idx(base, client_info->client_id);
    if(client_idx != TSB_FREE_ID) return STATUS_ALREADY_EXISTS; // Client ID already exists

    client_idx = get_client_idx(base, TSB_FREE_ID);
    if (client_idx == TSB_FREE_ID) return STATUS_FULL; // No available client
    
    *new_client_idx = client_idx;
    return STATUS_OK;
}


status_e tsb_register_client(tsb_base_t *base, const tsb_client_t *client_info){
    if(base == NULL || client_info == NULL || client_info->client_id == TSB_FREE_ID) return STATUS_INVALID_ARGUMENT;
    if (!base->initialized) return STATUS_NOT_INITIALIZED;
    base->platform_api.lock(base->platform_api.mutex_ctx);


    uint8_t new_client_idx;
    status_e ret = tsb_check_client_register(base, client_info, &new_client_idx);
    if (ret != STATUS_OK) {
        base->platform_api.unlock(base->platform_api.mutex_ctx);
        return ret;
    }

    tsb_client_t* temp    = &base->clients[new_client_idx];
    temp->client_id       = client_info->client_id;
    temp->client_callback = client_info->client_callback;

    base->platform_api.unlock(base->platform_api.mutex_ctx);
    
    return STATUS_OK;
}

static status_e check_subscription(tsb_base_t *base, uint8_t bus_id, uint8_t client_id, uint8_t *bus_idx, uint8_t *client_idx_in_bus){

    *bus_idx = get_bus_idx(base, bus_id);
    if(*bus_idx == TSB_FREE_ID) return STATUS_INVALID_ARGUMENT;

    uint8_t client_idx = get_client_idx(base, client_id);
    if(client_idx == TSB_FREE_ID) return STATUS_INVALID_ARGUMENT;


    *client_idx_in_bus = get_idx(base->buses[*bus_idx].client_ids, base->buses[*bus_idx].max_num_of_clients, client_id);
    if (*client_idx_in_bus != TSB_FREE_ID) return STATUS_ALREADY_EXISTS; // Already subscribed

    *client_idx_in_bus = get_idx(base->buses[*bus_idx].client_ids, base->buses[*bus_idx].max_num_of_clients, TSB_FREE_ID);
    if (*client_idx_in_bus == TSB_FREE_ID) return STATUS_FULL; // No space for new client


    return STATUS_OK;
}


status_e tsb_subscribe_bus(tsb_base_t *base, uint8_t bus_id, uint8_t client_id){
    if(base == NULL) return STATUS_INVALID_ARGUMENT;
    if (!base->initialized) return STATUS_NOT_INITIALIZED;
    base->platform_api.lock(base->platform_api.mutex_ctx);
    
    uint8_t bus_idx, client_idx_in_bus;

    status_e ret = check_subscription(base, bus_id, client_id, &bus_idx, &client_idx_in_bus);
    if(ret != STATUS_OK) {
        base->platform_api.unlock(base->platform_api.mutex_ctx);
        return ret;
    }

    tsb_bus_t* bus = &base->buses[bus_idx];

    bus->client_ids[client_idx_in_bus] = client_id;

    base->platform_api.unlock(base->platform_api.mutex_ctx);

    return STATUS_OK;
}

status_e tsb_send_data(tsb_base_t *base, uint8_t client_id, uint8_t bus_id, const tsb_memory_t *memory){
    if(base == NULL || memory == NULL || memory->memory == NULL) return STATUS_INVALID_ARGUMENT;
    if (!base->initialized) return STATUS_NOT_INITIALIZED;

    base->platform_api.lock(base->platform_api.mutex_ctx);
    uint8_t bus_idx = get_bus_idx(base, bus_id);
    uint8_t client_idx = get_client_idx(base, client_id);
    if(bus_idx == TSB_FREE_ID || client_idx == TSB_FREE_ID){
        base->platform_api.unlock(base->platform_api.mutex_ctx);
        return STATUS_INVALID_ARGUMENT;
    }

    tsb_bus_t*    bus       = &base->buses[bus_idx];
    tsb_client_t* client    = &base->clients[client_idx];

    if(client->client_id != bus->owner_client_id){
        base->platform_api.unlock(base->platform_api.mutex_ctx);
        return STATUS_PERMISSION_DENIED;
    }

    if(bus->busy){
        base->platform_api.unlock(base->platform_api.mutex_ctx);
        return STATUS_BUSY;
    }

    if (memory->memory_size > bus->memory_capacity || memory->memory_size <= 0) {
        base->platform_api.unlock(base->platform_api.mutex_ctx);
        return STATUS_INVALID_ARGUMENT;
    }

    bus->busy = 1;
    bus->memory.owner_client_id = client_id;
    bus->memory.memory_size = memory->memory_size;
    memcpy(bus->memory.memory, memory->memory, memory->memory_size);
    base->platform_api.unlock(base->platform_api.mutex_ctx);

    return STATUS_OK;
}

status_e tsb_thread(tsb_base_t *base){
    if(base == NULL) return STATUS_INVALID_ARGUMENT;
    if(!base->initialized) return STATUS_NOT_INITIALIZED;

    base->platform_api.lock(base->platform_api.mutex_ctx);

    for (uint8_t i = 0; i < base->bus_num; i++) {
        tsb_bus_t* bus = &base->buses[i];
        if(bus->busy){
            for (uint8_t j = 0; j < bus->max_num_of_clients; j++) {
                uint8_t client_id = bus->client_ids[j];
                if(client_id != TSB_FREE_ID){
                    uint8_t client_idx = get_client_idx(base, client_id);
                    if(client_idx != TSB_FREE_ID){
                        tsb_client_t* client = &base->clients[client_idx];

                        // base->platform_api.unlock(base->platform_api.mutex_ctx);
                        client->client_callback(&bus->memory);
                        // base->platform_api.lock(base->platform_api.mutex_ctx);
                    
                    }
                }
            }
            bus->busy = 0;
        }
    }
    
    base->platform_api.unlock(base->platform_api.mutex_ctx);

    return STATUS_OK;
}