/**                                                                                                      
 * DARE (Direct Access REplication)
 * 
 * Unreliable Datagrams (UD) over InfiniBand
 *
 * Copyright (c) 2016 HLRS, University of Stuttgart. All rights reserved.
 * 
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 *            Nakul Vyas <mailnakul@gmail.com>
 * 
 */
 
#ifndef DARE_IBV_UD_H
#define DARE_IBV_UD_H

#include <infiniband/verbs.h> /* OFED stuff */ 
#include "./dare_sm.h"
#include "./dare_ibv.h"
#include "./dare_config.h"
#include "./dare_ep_db.h"

#define REQ_MAJORITY 13
#define MCG_GID {255,1,0,0,0,2,201,133,0,0,0,0,0,0,0,0}

/* ================================================================== */
/* UD messages */
struct ud_hdr_t {
    uint64_t id;
    uint8_t type;
    union ibv_gid gid;
    //uint8_t pad[7];
    uint16_t slid;
};
typedef struct ud_hdr_t ud_hdr_t;

struct client_req_t {
    ud_hdr_t hdr;
    sm_cmd_t cmd;
};
typedef struct client_req_t client_req_t;

struct client_rep_t {
    ud_hdr_t hdr;
    sm_data_t data;
};
typedef struct client_rep_t client_rep_t;

struct reconf_req_t {
    ud_hdr_t hdr;
    uint8_t  idx_size;
};
typedef struct reconf_req_t reconf_req_t;

struct reconf_rep_t {
    ud_hdr_t   hdr;
    uint8_t    idx;
    dare_cid_t cid;
    uint64_t cid_idx;
    uint64_t head;
};
typedef struct reconf_rep_t reconf_rep_t;

struct rc_syn_t {
    ud_hdr_t hdr;
    rem_mem_t log_rm;
    rem_mem_t ctrl_rm;
    enum ibv_mtu mtu;
    //union ibv_gid gid;
    uint8_t idx;
    uint8_t size;
    uint8_t data[0];    // log & ctrl QPNs
};
typedef struct rc_syn_t rc_syn_t;

struct rc_ack_t {
    ud_hdr_t hdr;
    uint8_t idx;
};
typedef struct rc_ack_t rc_ack_t;

extern char* global_mgid; 

/* ================================================================== */ 

int ud_init( uint32_t receive_count );
int ud_start();
void ud_shutdown();

struct ibv_ah* ud_ah_create( uint16_t dlid, union ibv_gid dgid );
void ud_ah_destroy( struct ibv_ah* ah );

void get_tailq_message();
uint8_t ud_get_message();
int ud_join_cluster();
int ud_exchange_rc_info();
int ud_update_rc_info();
int ud_discover_servers();
int ud_establish_rc();

/* Client stuff */
int ud_send_clt_reply( uint16_t lid, uint64_t req_id, uint8_t type );
void ud_clt_answer_read_request(dare_ep_t *ep);

#endif /* DARE_IBV_UD_H */
