/**                                                                                                      
 * DARE (Direct Access REplication)
 *
 * Implementation of a DARE server
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#ifndef DARE_SERVER_H
#define DARE_SERVER_H 

#include <stdio.h>

#include <ev.h>
#include "../../../utils/rbtree/include/rbtree.h"
#include "./dare_log.h"
#include "./dare.h"
#include "./timer.h"

/* Server types */
#define SRV_TYPE_START  1
#define SRV_TYPE_JOIN   2
#define SRV_TYPE_LOGGP  3

/* LogGP param types */
#define LOGGP_PARAM_O   1
#define LOGGP_PARAM_OP  2
#define LOGGP_PARAM_L   3
#define LOGGP_PARAM_OPX 4

/* Retry period before failures in ms */
extern const double retry_exec_period;

/* Heartbeat period in ms */
extern double hb_period;
extern uint64_t elec_timeout_low;
extern uint64_t elec_timeout_high;
extern double rc_info_period;
extern double retransmit_period;
extern double log_pruning_period;

/**
 * The state identifier (SID)
 * the SID is a 64-bit value [TERM|L|IDX], where
    * TERM is the current term
    * L is the leader flag, set when there is a leader
    * IDX is the index of the server that cause the last SID update
 */
/* The IDX consists of the 8 least significant bits (lsbs) */
#define SID_GET_IDX(sid) (uint8_t)((sid) & (0xFF))
#define SID_SET_IDX(sid, idx) (sid) = (idx | ((sid >> 8) << 8))
/* The L flag is the 9th lsb */
#define SID_GET_L(sid) ((sid) & (1 << 8))
#define SID_SET_L(sid) (sid) |= 1 << 8
#define SID_UNSET_L(sid) (sid) &= ~(1 << 8)
/* The TERM consists of the most significant 55 bits */
#define SID_GET_TERM(sid) ((sid) >> 9)
#define SID_SET_TERM(sid, term) (sid) = (((term) << 9) | ((sid) & 0x1FF))

#define PRINT_SID(sid) text(log_fp,     \
    " [%010"PRIu64"|%d|%03"PRIu8"] ", \
    SID_GET_TERM(sid),  \
    (SID_GET_L(sid) ? 1 : 0),   \
    SID_GET_IDX(sid))
#define PRINT_SID_(sid) PRINT_SID(sid); text(log_fp, "\n");

#define IS_SID_NEW(sid) (!SID_GET_L(sid) && (SID_GET_TERM(sid) == 0))
#define SID_NULL 0xFF
#define SID_DEAD 0xFFFFFFFFFFFFFFFF

/* Number of fail communication attempts before considering a remote 
server as permanently failed */
#define PERMANENT_FAILURE   2

/* Normal operation (log replication) steps */
#define LR_GET_WRITE      1
#define LR_GET_NCE_LEN    2
#define LR_GET_NCE        3
#define LR_SET_END        4
#define LR_UPDATE_LOG     5
#define LR_UPDATE_END     6

struct server_t {
    uint64_t next_wr_id;    // next WR ID to wait for
    uint64_t cached_end_offset; // the new end offset if the log update succeeds
    uint64_t last_get_read_ssn; // ssn of the last get read operation
    void *ep;               // endpoint data (network related)
    uint8_t fail_count;     // number of failures detected
    uint8_t next_lr_step;   // next log replication step 
    uint8_t send_flag;      // flag set for posting send for this EP
    uint8_t send_count;     // number of sends poster for current step
};

//typedef struct server_t server_t;

struct vote_req_t {
    uint64_t sid;
    uint64_t index;
    uint64_t term;
    dare_cid_t cid;
};
typedef struct vote_req_t vote_req_t;

struct prv_data_t {
    uint64_t vote_sid;  // SID of last vote given
                        // on recovery need to retrieve this from a 
                        // remote server and update own SID to at least 
                        // this SID
};
typedef struct prv_data_t prv_data_t;

struct sm_rep_t {
    uint64_t sid;
    uint64_t raddr;
    uint32_t rkey;
    uint32_t len;
};
typedef struct sm_rep_t sm_rep_t;

struct ctrl_data_t {
    /* State identified (SID) */
    uint64_t    sid;
    
    /* DARE arrays */
    vote_req_t    vote_req[MAX_SERVER_COUNT];       /* vote requests */
    log_offsets_t log_offsets[MAX_SERVER_COUNT];	/* log offsets */
    sm_rep_t      sm_rep[MAX_SERVER_COUNT];
    uint64_t      sm_req[MAX_SERVER_COUNT];
    uint64_t 	  hb[MAX_SERVER_COUNT];             /* heartbeat array */ 
    uint64_t      vote_ack[MAX_SERVER_COUNT];
    uint64_t      rsid[MAX_SERVER_COUNT];   /* for remote terms & indexes */
    uint64_t      apply_offsets[MAX_SERVER_COUNT];   /* apply offsets */
    
    /* Remote private data */
    prv_data_t  prv_data[MAX_SERVER_COUNT];    // private data
};
typedef struct ctrl_data_t ctrl_data_t;

struct dare_server_input_t {
    FILE* log;
    char* name;
    char* output;
    uint8_t srv_type;
    uint8_t sm_type;
    uint8_t group_size;
    uint8_t server_idx;
    
    proxy_do_action_cb_t do_action;
    proxy_store_cmd_cb_t store_cmd;
    proxy_create_db_snapshot_cb_t create_db_snapshot;
    proxy_get_db_size_cb_t get_db_size;
    proxy_apply_db_snapshot_cb_t apply_db_snapshot;
    proxy_update_state_cb_t update_state;
    char config_path[128];
    void* up_para;
};
typedef struct dare_server_input_t dare_server_input_t;

struct dare_loggp_t {
    double o[2],
        o_ninline,
        o_poll,
        o_poll_x,
        L[2],
        G[3];
};
typedef struct dare_loggp_t dare_loggp_t;

struct dare_server_data_t {
    dare_server_input_t *input;
    
    server_config_t config; // configuration 
    
    ctrl_data_t *ctrl_data;  // control data (state & private data)
    dare_log_t  *log;       // local log (remotely accessible)
    dare_sm_t   *sm;        // local state machine
    snapshot_t  *prereg_snapshot;
    snapshot_t  *snapshot;
    
    struct rb_root endpoints;   // RB-tree with remote endpoints
    uint64_t last_write_csm_idx;
    uint64_t last_cmt_write_csm_idx;
    
    struct ev_loop *loop;   // loop for EV library

    FILE* output_fp;
    dare_loggp_t loggp;
    
    HRT_TIMESTAMP_T t1, t2;
};
typedef struct dare_server_data_t dare_server_data_t;
/* ================================================================== */

void *dare_server_init( void *arg );
void dare_server_shutdown();

void server_to_follower();
int server_update_sid( uint64_t new_sid, uint64_t old_sid );
int is_leader();
uint8_t get_node_id();

#endif /* DARE_SERVER_H */
