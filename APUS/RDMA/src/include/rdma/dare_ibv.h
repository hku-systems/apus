#include <infiniband/verbs.h> /* OFED IB verbs */
#include "dare.h"
 
#ifndef DARE_IBV_H
#define DARE_IBV_H

#define mtu_value(mtu) \
    ((mtu == IBV_MTU_256) ? 256 :    \
    (mtu == IBV_MTU_512) ? 512 :    \
    (mtu == IBV_MTU_1024) ? 1024 :  \
    (mtu == IBV_MTU_2048) ? 2048 :  \
    (mtu == IBV_MTU_4096) ? 4096 : 0)

struct rem_mem_t {
    uint64_t raddr;
    uint32_t rkey;
};
typedef struct rem_mem_t rem_mem_t;

struct rc_qp_t {
    struct ibv_qp *qp;          // RC QP
    uint32_t qpn;               // remote QP number
    uint32_t send_count;        // number of posted sends
}; 
typedef struct rc_qp_t rc_qp_t;

struct rc_cq_t {
    struct ibv_cq *cq;          // RC QP
}; 
typedef struct rc_cq_t rc_cq_t;

/* Endpoint RC info */
struct rc_ep_t {
    rem_mem_t rmt_mr;    // remote memory regions
    rc_qp_t   rc_qp;     // RC QPs (LOG)
    rc_cq_t   rc_cq;     // RC CQs (LOG)
};
typedef struct rc_ep_t rc_ep_t;

struct dare_ib_ep_t {
    rc_ep_t rc_ep;  // RC info
    int rc_connected;
};
typedef struct dare_ib_ep_t dare_ib_ep_t;

struct dare_ib_device_t {
    /* General fields */
    struct ibv_device *ib_dev;
    struct ibv_context *ib_dev_context;
    struct ibv_device_attr ib_dev_attr;  
    uint16_t pkey_index;
    uint8_t port_num;       // port number 
    uint32_t mtu;           // MTU for this device
    uint16_t lid;           // local ID for this device     
    int gid_idx;            // default not used
    
    /* QPs for inter-server communication - RC */
    struct ibv_pd *rc_pd;
    int rc_cqe;
    struct ibv_mr *lcl_mr;
    uint32_t      rc_max_inline_data;
    uint32_t      rc_max_send_wr;

    void *udata;
};
typedef struct dare_ib_device_t dare_ib_device_t;

extern dare_ib_device_t *dare_ib_device;

/* ================================================================== */

/* Init and cleaning up */
int dare_init_ib_device();
int dare_init_ib_srv_data( void *data );
int dare_init_ib_rc();
void dare_ib_srv_shutdown();
int find_max_inline(struct ibv_context *context, struct ibv_pd *pd, uint32_t *max_inline_arg );

#endif /* DARE_IBV_H */
