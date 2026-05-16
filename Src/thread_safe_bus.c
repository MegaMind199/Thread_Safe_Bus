#include "thread_safe_bus.h"

/// TODO correct idxs and ids at first when you will come here 

int8_t get_idx(int8_t *array, uint8_t size, int8_t value){
    for (uint8_t i = 0; i < size; i++) {
        if(array[i] == value) return i;
    }
    return -1;
}

status_e tsb_init(tsb_base_t *base, tsb_base_init_t *init){
    
    if(base == NULL || init == NULL) return STATUS_INVALID_ARGUMENT;

    base->mutex_ctx = init->mutex_ctx;
    base->bus_num   = init->bus_num;
    base->client_num= init->client_num;
    base->buses     = malloc(sizeof(tsb_bus_t)    * init->bus_num);
    base->clients   = malloc(sizeof(tsb_client_t) * init->client_num);

    for (uint8_t i = 0; i < init->bus_num; i++) {
        base->buses[i].busy     = 0;
        base->buses[i].bus_id   = -1;
    }

    for (uint8_t i = 0; i < init->client_num; i++) {
        base->clients[i].client_id = -1;
    }

    if(base->buses == NULL || base->clients == NULL){
        free(base->buses);
        free(base->clients);
        return STATUS_NULL_POINTER;
    }

    return STATUS_OK;
}

status_e tsb_register_bus(tsb_base_t *base, tsb_bus_t *register_info){
    if(base == NULL || register_info == NULL) return STATUS_INVALID_ARGUMENT;

    base->mutex_ctx.lock(base->mutex_ctx.mutex_ctx);

    tsb_bus_t* temp = &base->buses[register_info->bus_id];

    temp->busy               = 0;
    temp->bus_id             = register_info->bus_id;
    temp->owner_client_id    = register_info->owner_client_id;
    temp->max_num_of_clients = register_info->max_num_of_clients;
    temp->client_ids         = malloc(sizeof(int8_t) * register_info->max_num_of_clients);

    temp->memory.bus_id      = register_info->bus_id;
    temp->memory.memory_size = register_info->memory.memory_size;
    temp->memory.memory      = malloc(sizeof(uint8_t) * register_info->memory.memory_size);

    if(temp->client_ids == NULL || temp->memory.memory == NULL){
        free(temp->client_ids);
        free(temp->memory.memory);
        base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);
        return STATUS_NULL_POINTER;
    }

    memset(temp->client_ids, -1, sizeof(int8_t) * register_info->max_num_of_clients);

    base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);

    return STATUS_OK;   
}

status_e tsb_register_client(tsb_base_t *base, tsb_client_t *client_info){
    if(base == NULL || client_info == NULL) return STATUS_INVALID_ARGUMENT;

    base->mutex_ctx.lock(base->mutex_ctx.mutex_ctx);


    tsb_client_t* temp    = &base->clients[client_info->client_id];
    temp->client_id       = client_info->client_id;
    temp->client_callback = client_info->client_callback;

    base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);
    
    return STATUS_OK;
}

status_e check_subscription(tsb_base_t *base, uint8_t bus_id, uint8_t client_id, int8_t *bus_idx, int8_t *client_idx){
    if(base == NULL) return STATUS_INVALID_ARGUMENT;

    *bus_idx = -1;
    for(uint8_t i = 0; i < base->bus_num; i++){
        if(base->buses[i].bus_id == bus_id){
            *bus_idx = i;
            break;
        }
    }
    if(*bus_idx == -1) return STATUS_INVALID_ARGUMENT;

    *client_idx = -1;
    for(uint8_t i = 0; i < base->client_num; i++){
        if(base->clients[i].client_id == client_id){
            *client_idx = i;
            break;
        }
    }
    if(*client_idx == -1) return STATUS_INVALID_ARGUMENT;
    return STATUS_OK;
}

status_e tsb_subscribe_bus(tsb_base_t *base, uint8_t bus_id, uint8_t client_id){
    int8_t bus_idx, client_idx;
    status_e ret = check_subscription(base, bus_id, client_id, &bus_idx, &client_idx);

    if(ret != STATUS_OK) return ret;

    base->mutex_ctx.lock(base->mutex_ctx.mutex_ctx);
    tsb_bus_t* bus = &base->buses[bus_idx];

    int8_t client_idx = get_idx(bus->client_ids, bus->max_num_of_clients, client_id);
    if (client_idx != -1) {
        base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);
        return STATUS_OK; // Already subscribed
    }

    int8_t empty_idx = get_idx(bus->client_ids, bus->max_num_of_clients, -1);
    if (empty_idx == -1) {
        base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);
        return STATUS_ERROR; // No space for new client
    }
    
    bus->client_ids[empty_idx] = client_id;

    base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);

    return STATUS_OK;
}

status_e tsb_send_data(tsb_base_t *base, uint8_t client_id, uint8_t bus_id, tsb_memory_t *memory){
    if(base == NULL || memory == NULL) return STATUS_INVALID_ARGUMENT;

    base->mutex_ctx.lock(base->mutex_ctx.mutex_ctx);

    tsb_bus_t* bus = &base->buses[bus_id];
    if(bus == NULL){
        base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);
        return STATUS_INVALID_ARGUMENT;
    }

    tsb_client_t* client = &base->clients[client_id];
    if(client == NULL){
        base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);
        return STATUS_INVALID_ARGUMENT;
    }

    if(client->client_id != bus->owner_client_id){
        base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);
        return STATUS_PERMISION_DENIED;
    }

    if(bus->busy){
        base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);
        return STATUS_ERROR;
    }

    bus->busy = 1;
    base->mutex_ctx.unlock(base->mutex_ctx.mutex_ctx);

    return STATUS_OK;
}

status_e tsb_thread(tsb_base_t *base){
    if(base == NULL) return STATUS_INVALID_ARGUMENT;

}