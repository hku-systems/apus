/**                                                                                                      
 * DARE (Direct Access REplication)
 * 
 * State machine abstraction
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#ifndef DARE_SM_H
#define DARE_SM_H

#include "./dare_kvs_sm.h"

/* SM types */
#define SM_NULL 1
#define SM_KVS  2
#define SM_FS   3

/* SM command - can be interpreted only by the SM */
struct sm_cmd_t {
    uint16_t    len;
    uint8_t cmd[0];
};
typedef struct sm_cmd_t sm_cmd_t;

/* SM data - as answer to a command */
struct sm_data_t {
    uint16_t    len;
    uint8_t data[0];
};
typedef struct sm_data_t sm_data_t;
typedef struct dare_sm_t dare_sm_t;

/* Destroy the state machine */
typedef void (*destroy_cb_t)(dare_sm_t *sm);
/* Apply a command to the state machine */
typedef int (*apply_cmd_cb_t)(dare_sm_t *sm, sm_cmd_t *cmd, sm_data_t *data);

typedef void (*proxy_store_cmd_cb_t)(void* data,void *arg);
typedef void (*proxy_do_action_cb_t)(uint16_t clt_id,uint8_t type,size_t data_size,void* data,void *arg);
typedef void (*proxy_create_db_snapshot_cb_t)(void *snapshot,void *arg);
typedef uint32_t (*proxy_get_db_size_cb_t)(void *arg);
typedef int (*proxy_apply_db_snapshot_cb_t)(void *snapshot,uint32_t size,void *arg);
typedef void (*proxy_update_state_cb_t)(void *arg);

struct dare_sm_t {
    destroy_cb_t   destroy;
    apply_cmd_cb_t apply_cmd;

    proxy_store_cmd_cb_t proxy_store_cmd;
    proxy_do_action_cb_t proxy_do_action;
    proxy_get_db_size_cb_t proxy_get_db_size;
    proxy_create_db_snapshot_cb_t proxy_create_db_snapshot;
    proxy_apply_db_snapshot_cb_t proxy_apply_db_snapshot;
    proxy_update_state_cb_t proxy_update_state;
    void* up_para;
};

/* ================================================================== */

dare_sm_t* create_kvs_sm( uint32_t size );


#endif /* DARE_SM_H */
