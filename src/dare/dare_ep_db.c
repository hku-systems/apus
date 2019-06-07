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
 
#include <stdlib.h>

#include "../include/dare/debug.h"
#include "../include/dare/dare_ibv_ud.h"
#include "../include/dare/dare_ibv_rc.h"

#include "../include/dare/dare_ep_db.h"

/* ================================================================== */

static void 
free_ep(dare_ep_t *ep);

/* ================================================================== */

dare_ep_t* ep_search( struct rb_root *root, const uint16_t lid )
{
    struct rb_node *node = root->rb_node;

    while (node) 
    {
        dare_ep_t *ep = container_of(node, dare_ep_t, node);

        if (lid < ep->ud_ep.lid)
            node = node->rb_left;
        else if (lid > ep->ud_ep.lid)
            node = node->rb_right;
        else
            return ep;
    }
    return NULL;
}

dare_ep_t* ep_insert( struct rb_root *root, const uint16_t lid, const union ibv_gid dest_gid )
{
    dare_ep_t *ep;
    struct rb_node **new = &(root->rb_node), *parent = NULL;
    
    while (*new) 
    {
        dare_ep_t *this = container_of(*new, dare_ep_t, node);
        
        parent = *new;
        if (lid < this->ud_ep.lid)
            new = &((*new)->rb_left);
        else if (lid > this->ud_ep.lid)
            new = &((*new)->rb_right);
        else
            return NULL;
    }
    
    /* Create new rr */
    ep = (dare_ep_t*)malloc(sizeof(dare_ep_t));
    ep->ud_ep.lid = lid;
    ep->ud_ep.gid = dest_gid;
    ep->last_req_id = 0;
    ep->cid_idx = 0;
    ep->committed = 0;
    ep->wait_for_idx = 0;
    
    /* Create AH */
    ep->ud_ep.ah = ud_ah_create(lid, dest_gid);
    

    /* Add new node and rebalance tree. */
    rb_link_node(&ep->node, parent, new);
    rb_insert_color(&ep->node, root);

    return ep;
}

void ep_erase( struct rb_root *root, const uint16_t lid )
{
    dare_ep_t *ep = ep_search(root, lid);

    if (ep) 
    {
        rb_erase(&ep->node, root);
        free_ep(ep);
    }
}

void ep_db_print( struct rb_root *root )
{
    struct rb_node *node;
    dare_ep_t *ep;
    
    for (node = rb_first(root); node; node = rb_next(node)) 
    {
        ep = rb_entry(node, dare_ep_t, node);
        info(log_fp, "[%"PRIu16": qpn=%"PRIu32"] ", 
            ep->ud_ep.lid, ep->ud_ep.qpn);
    }
}

void ep_db_free( struct rb_root *root )
{
    struct rb_node *node;
    dare_ep_t *ep;
    
    for (node = rb_first_postorder(root); node;) 
    {
        ep = rb_entry(node, dare_ep_t, node);
        node = rb_next_postorder(node);
        free_ep(ep);
    }
}

void ep_dp_reset_wait_idx( struct rb_root *root )
{
    struct rb_node *node;
    dare_ep_t *ep;
    
    for (node = rb_first(root); node; node = rb_next(node)) 
    {
        ep = rb_entry(node, dare_ep_t, node);
        ep->wait_for_idx = 0;
    }
}

void ep_dp_reply_read_req( struct rb_root *root, uint64_t idx )
{
    int rc;
    struct rb_node *node;
    dare_ep_t *ep;
    int verify_leadership = 0;
    int leader = 0;
    
    for (node = rb_first(root); node; node = rb_next(node)) 
    {
        ep = rb_entry(node, dare_ep_t, node);
        if (!ep->wait_for_idx) continue;
        if (!verify_leadership) {
            /* Verify leadership */
            rc = rc_verify_leadership(&leader);
            if (0 != rc) {
                error(log_fp, "Cannot verify leadership\n");
            }
            if (0 == leader) {
                /* No longer the leader; reset the wait idx */
                ep_dp_reset_wait_idx(root);
                return;
            }
            verify_leadership = 1;
        }
        if (ep->wait_for_idx < idx) {
            ud_clt_answer_read_request(ep);
        }
    }
}

/* ================================================================== */

static void 
free_ep(dare_ep_t *ep)
{
    ud_ah_destroy(ep->ud_ep.ah);
    free(ep);
}
