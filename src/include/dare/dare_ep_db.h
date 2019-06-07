/**                                                                                                      
 * DARE (Direct Access REplication)
 *
 * Endpoint database
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#ifndef DARE_EP_DB_H
#define DARE_EP_DB_H

#include "../../../utils/rbtree/include/rbtree.h"
#include "./dare_ibv.h"

/* ================================================================== */

struct dare_ep_t {
    struct rb_node node;
    ud_ep_t ud_ep;
    uint8_t last_read_request[128];
    uint64_t wait_for_idx;
    uint64_t last_req_id;   /* this is the ID of the last request from 
                            this endpoint that I answer; ignore requests 
                            with lower IDs */
    uint64_t cid_idx;
    int committed;
};
typedef struct dare_ep_t dare_ep_t;

/* ================================================================== */

dare_ep_t* ep_search( struct rb_root *root, const uint16_t lid );
dare_ep_t* ep_insert( struct rb_root *root, const uint16_t lid, const union ibv_gid dest_gid );
void ep_erase( struct rb_root *root, const uint16_t lid );
void ep_db_print( struct rb_root *root );
void ep_db_free( struct rb_root *root );
void ep_dp_reset_wait_idx( struct rb_root *root );
void ep_dp_reply_read_req( struct rb_root *root, uint64_t idx );


#endif /* DARE_EP_DB_H */


