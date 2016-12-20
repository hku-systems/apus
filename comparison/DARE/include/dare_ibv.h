/**                                                                                                      
 * DARE (Direct Access REplication)
 *
 * Network module for the DARE consensus algorithm (IB verbs)
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#include <infiniband/verbs.h> /* OFED IB verbs */
#include <dare.h>
 
#ifndef DARE_IBV_H
#define DARE_IBV_H

#define DARE_WR_COUNT    32
#define IB_PKEY_MASK 0x7fff

#define IBV_SERVER  1
#define IBV_CLIENT  2


#define mtu_value(mtu) \
    ((mtu == IBV_MTU_256) ? 256 :    \
    (mtu == IBV_MTU_512) ? 512 :    \
    (mtu == IBV_MTU_1024) ? 1024 :  \
    (mtu == IBV_MTU_2048) ? 2048 :  \
    (mtu == IBV_MTU_4096) ? 4096 : 0)

#define qp_state_to_str(state) \
   ((state == IBV_QPS_RESET) ? "RESET" : \
   (state == IBV_QPS_INIT) ? "INIT" : \
   (state == IBV_QPS_RTR) ? "RTR" : \
   (state == IBV_QPS_RTS) ? "RTS" : \
   (state == IBV_QPS_ERR) ? "ERR" : "X")

#define CTRL_PSN 13
#define LOG_PSN 55
#define LOG_QP 1
#define CTRL_QP 0

#define HB_CNT_DELAY 3

/* Endpoint UD info */
struct ud_ep_t {
    uint16_t lid;
    uint32_t qpn;
    struct ibv_ah *ah;
};
typedef struct ud_ep_t ud_ep_t;

struct rem_mem_t {
    uint64_t raddr;
    uint32_t rkey;
};
typedef struct rem_mem_t rem_mem_t;

#define RC_QP_ACTIVE    0
#define RC_QP_BLOCKED   1
#define RC_QP_ERROR     2

struct rc_qp_t {
    struct ibv_qp *qp;          // RC QP
    uint64_t signaled_wr_id;    // ID of signaled WR (to avoid overflow)
    uint32_t qpn;               // remote QP number
    uint32_t send_count;        // number of posted sends
    uint8_t  state;             // QP's state
}; 
typedef struct rc_qp_t rc_qp_t;

/* Endpoint RC info */
struct rc_ep_t {
    rem_mem_t rmt_mr[2];    // remote memory regions
    rc_qp_t   rc_qp[2];     // RC QPs (LOG & CTRL)
};
typedef struct rc_ep_t rc_ep_t;

struct dare_ib_ep_t {
    ud_ep_t ud_ep;  // UD info
    rc_ep_t rc_ep;  // RC info
    uint32_t mtu;
    int rc_connected;
    int log_access;
};
typedef struct dare_ib_ep_t dare_ib_ep_t;

struct dare_ib_device_t {
    /* General fields */
    struct ibv_device *ib_dev;
    struct ibv_context *ib_dev_context;
    struct ibv_device_attr ib_dev_attr;
    uint16_t pkey_index;    
    uint8_t port_num;       // port number 
    enum ibv_mtu mtu;       // MTU for this device
    uint16_t lid;           // local ID for this device        

    /* QP for listening for clients requests - UD */
    struct ibv_pd           *ud_pd;
    struct ibv_qp           *ud_qp;
    struct ibv_cq           *ud_rcq;
    struct ibv_cq           *ud_scq;
    int                     ud_rcqe;
    void                    **ud_recv_bufs;
    struct ibv_mr           **ud_recv_mrs;
    void                    *ud_send_buf;
    struct ibv_mr           *ud_send_mr;
    uint32_t                ud_max_inline_data;
    uint64_t  request_id;
    
    /* Multicast */
    struct ibv_ah *ib_mcast_ah;
    union ibv_gid mgid;
    uint16_t      mlid;
    
    /* QPs for inter-server communication - RC */
    struct ibv_pd *rc_pd;
    struct ibv_cq *rc_cq[2];
    int           rc_cqe;
    struct ibv_wc *rc_wc_array;
    struct ibv_mr *lcl_mr[2];
    uint32_t      rc_max_inline_data;
    uint32_t      rc_max_send_wr;
    
    /* Snapshot */
    struct ibv_mr *prereg_snapshot_mr;
    struct ibv_mr *snapshot_mr;
    
    int ulp_type;
    void *udata;
};
typedef struct dare_ib_device_t dare_ib_device_t;

/* ================================================================== */

/* Init and cleaning up */
int dare_init_ib_device();
int dare_start_ib_ud();
int dare_init_ib_srv_data( void *data );
int dare_init_ib_clt_data( void *data );
int dare_init_ib_rc();
void dare_ib_srv_shutdown();
void dare_ib_clt_shutdown();
void dare_ib_destroy_ep( uint8_t idx );

/* Starting a server */
uint8_t dare_ib_poll_ud_queue();
int dare_ib_join_cluster();
int dare_ib_exchange_rc_info();
int dare_ib_update_rc_info();
int dare_ib_get_replicated_vote();
int dare_ib_send_sm_request();
int dare_ib_send_sm_reply( uint8_t idx, void *s, int reg_mem );
int dare_ib_recover_sm( uint8_t idx );
int dare_ib_recover_log();

/* HB mechanism */
int dare_ib_send_hb();
int dare_ib_send_hb_reply( uint8_t idx );

/* Leader election */
int dare_ib_send_vote_request();
int dare_ib_replicate_vote();
int dare_ib_send_vote_ack();

/* Normal operation */
int dare_ib_establish_leadership();
int dare_ib_write_remote_logs( int wait_for_commit );
int dare_ib_get_remote_apply_offsets();

/* Handle client requests */
int dare_ib_apply_cmd_locally();
int dare_ib_create_clt_request();
int dare_ib_create_clt_downsize_request();
int dare_ib_resend_clt_request();
int dare_ib_send_clt_reply( uint16_t lid, uint64_t req_id, uint8_t type );

/* Handle QPs state */
void dare_ib_disconnect_server( uint8_t idx );
int dare_ib_revoke_log_access();
int dare_ib_restore_log_access();

/* LogGP */
double dare_ib_get_loggp_params( uint32_t size, int type, int *poll_count, int write, int inline_flag );
double dare_ib_loggp_prtt( int n, double delay, uint32_t size, int inline_flag );
int dare_ib_loggp_exit();

void print_rc_info();
int print_qp_state( void *qp );
int dare_ib_print_ud_qp();

void dare_ib_send_msg();
int find_max_inline( struct ibv_context *context, 
                     struct ibv_pd *pd,
                     uint32_t *max_inline_arg );

#endif /* DARE_IBV_H */
