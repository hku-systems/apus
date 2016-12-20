/**                                                                                                      
 * DARE (Direct Access REplication)
 * 
 * Reliable Connection (RC) over InfiniBand
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */

#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>

#include <dare_ibv_rc.h>
#include <dare_ibv_ud.h>
#include <dare_ibv.h>
#include <dare_server.h>
#include <timer.h>
#include <math.h>

/* Return code for RC operations */
#define RC_ERROR      1
#define RC_SUCCESS    0
#define RC_INSUCCESS  -1

/* Return code for handling WCs */
#define WC_SUCCESS      0
#define WC_ERROR        1
#define WC_FAILURE      2
#define WC_SOFTWARE_BUG 3

extern FILE *log_fp;
extern int terminate; 

/* InfiniBand device */
extern dare_ib_device_t *dare_ib_device;
#define IBDEV dare_ib_device
#define SRV_DATA ((dare_server_data_t*)dare_ib_device->udata)
#define CLT_DATA ((dare_client_data_t*)dare_ib_device->udata)

uint64_t ssn;   // Send Sequence Number
int wa_flag;

/* ================================================================== */

static int
rc_prerequisite();
static int
rc_memory_reg();
static void
rc_memory_dereg();
static int 
rc_qp_create( dare_ib_ep_t* ep );
static void 
rc_qp_destroy( dare_ib_ep_t* ep);
static int 
rc_qp_init_to_rtr( dare_ib_ep_t *ep, int qp_id );
static int 
rc_qp_rtr_to_rts( dare_ib_ep_t *ep, int qp_id );
static int 
rc_qp_reset_to_rts(dare_ib_ep_t *ep, int qp_id);
static int
rc_qp_reset( dare_ib_ep_t *ep, int qp_id );
static int 
rc_qp_reset_to_init( dare_ib_ep_t *ep, int qp_id );
static int 
rc_qp_restart( dare_ib_ep_t *ep, int qp_id );
static int 
post_send( uint8_t server_id, 
           int qp_id,
           void *buf,
           uint32_t len,
           struct ibv_mr *mr,
           enum ibv_wr_opcode opcode,
           int signaled,
           rem_mem_t rm,
           int *posted_sends );
static int
empty_completion_queue( uint8_t server_id, 
                        int qp_id,
                        int wait_signaled_wr,
                        int *posted_sends );
#if 0
static int 
rc_post_cas( struct ibv_qp *qp,
             void *buf,
             struct ibv_mr *mr,
             uint8_t idx,
             uint64_t old_value,
             uint64_t new_value,
             int signaled,
             rem_mem_t rm );
#endif
static int
wait_for_majority( int *posted_sends, int qp_id );
static int
wait_for_one( int *posted_sends, int qp_id );
static void
handle_lr_work_completion( uint8_t idx, int wc_rc );
static int
handle_work_completion( struct ibv_wc *wc, int qp_id );

static int
log_adjustment();
static int
update_remote_logs();
static int 
cmpfunc_offset( const void *a, const void *b );
static int 
cmpfunc_uint64( const void *a, const void *b );


/* ================================================================== */
/* Init and cleaning up */
#if 1
/**
 * Initialize RC data
 */
int rc_init()
{
    int rc, i;

    rc = rc_prerequisite();
    if (0 != rc) {
        error_return(1, log_fp, "Cannot create RC prerequisite\n");
    }

    /* Register memory for RC */
    rc = rc_memory_reg();
    if (0 != rc) {
        error_return(1, log_fp, "Cannot register RC memory\n");
    }
    
    /* Create QPs for RC communication */
    for (i = 0; i < SRV_DATA->config.len; i++) {
        dare_ib_ep_t* ep = (dare_ib_ep_t*)
                        SRV_DATA->config.servers[i].ep;
        
        /* Create QPs for this endpoint */
        rc = rc_qp_create(ep);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot create QPs\n");
        }
    }
    
    return 0;
}

void rc_free()
{
    int i;
    if (NULL != SRV_DATA) {
        for (i = 0; i < SRV_DATA->config.len; i++) {
            dare_ib_ep_t* ep = (dare_ib_ep_t*)
                            SRV_DATA->config.servers[i].ep;
            rc_qp_destroy(ep);             
        }
    }
    if (NULL != IBDEV->rc_wc_array) {
        free(IBDEV->rc_wc_array);
        IBDEV->rc_wc_array = NULL;
    }
    if (NULL != IBDEV->rc_cq[LOG_QP]) {
        ibv_destroy_cq(IBDEV->rc_cq[LOG_QP]);
    }
    if (NULL != IBDEV->rc_cq[CTRL_QP]) {
        ibv_destroy_cq(IBDEV->rc_cq[CTRL_QP]);
    }
    if (NULL != IBDEV->rc_pd) {
        ibv_dealloc_pd(IBDEV->rc_pd);
    }
    rc_memory_dereg();
}

static int
rc_prerequisite()
{
    if (SRV_TYPE_LOGGP == SRV_DATA->input->srv_type) { 
        IBDEV->rc_max_send_wr = IBDEV->ib_dev_attr.max_qp_wr;
    }
    else {
        /* To avoid waiting for failed WR, we need a large enough queue;
        we use an estimation of the amount of time needed by IB to identify 
        a failure in transmission (i.e., retry_exec_period) */
        // TODO FIXME this is kind of guessing
        IBDEV->rc_max_send_wr = 2 * ceil(retry_exec_period / hb_period);
        if (IBDEV->rc_max_send_wr > IBDEV->ib_dev_attr.max_qp_wr) {
            IBDEV->rc_max_send_wr = IBDEV->ib_dev_attr.max_qp_wr;
        }
        info(log_fp, "# IBDEV->rc_max_send_wr = %"PRIu32"\n", IBDEV->rc_max_send_wr);
    }
    
    //IBDEV->rc_max_send_wr = 128;
    //IBDEV->rc_max_send_wr = 16;
    IBDEV->rc_cqe = MAX_SERVER_COUNT * IBDEV->rc_max_send_wr;
    if (IBDEV->rc_cqe > IBDEV->ib_dev_attr.max_cqe) {
        IBDEV->rc_cqe = IBDEV->ib_dev_attr.max_cqe;
    }
    info(log_fp, "# IBDEV->rc_cqe = %d\n", IBDEV->rc_cqe);
    
    /* Allocate a RC protection domain */
    IBDEV->rc_pd = ibv_alloc_pd(IBDEV->ib_dev_context);
    if (NULL == IBDEV->rc_pd) {
        error_return(1, log_fp, "Cannot create PD\n");
    }

    /* Create a RC completion queue */
    IBDEV->rc_cq[LOG_QP] = ibv_create_cq(IBDEV->ib_dev_context, 
                                   IBDEV->rc_cqe, NULL, NULL, 0);
    if (NULL == IBDEV->rc_cq[LOG_QP]) {
        error_return(1, log_fp, "Cannot create LOG CQ\n");
    }
    IBDEV->rc_cq[CTRL_QP] = ibv_create_cq(IBDEV->ib_dev_context, 
                                   IBDEV->rc_cqe, NULL, NULL, 0);
    if (NULL == IBDEV->rc_cq[CTRL_QP]) {
        error_return(1, log_fp, "Cannot create CTRL CQ\n");
    }

    if (0 != find_max_inline(IBDEV->ib_dev_context,
                            IBDEV->rc_pd,
                            &IBDEV->rc_max_inline_data))
    {
        error_return(1, log_fp, "Cannot find max RC inline data\n");
    }
    info(log_fp, "# MAX_INLINE_DATA = %"PRIu32"\n", IBDEV->rc_max_inline_data);
    
    /* Allocate array for work completion */
    IBDEV->rc_wc_array = (struct ibv_wc*)
            malloc(IBDEV->rc_cqe * sizeof(struct ibv_wc));
    if (NULL == IBDEV->rc_wc_array) {
        error_return(1, log_fp, "Cannot allocate array for WC\n");
    }
    return 0;
}

static int
rc_memory_reg()
{
    /* Register memory for control data: state & private data */
    //debug(log_fp, "CTRL mem addr %"PRIu64"\n", (uint64_t)SRV_DATA->ctrl_data);
    IBDEV->lcl_mr[CTRL_QP] = ibv_reg_mr(IBDEV->rc_pd,
            SRV_DATA->ctrl_data, sizeof(ctrl_data_t), 
            IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | 
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);
    if (NULL == IBDEV->lcl_mr[CTRL_QP]) {
        error_return(1, log_fp, "Cannot register memory because %s\n", 
                    strerror(errno));
    }
   
    /* Register memory for local log */    
    IBDEV->lcl_mr[LOG_QP] = ibv_reg_mr(IBDEV->rc_pd,
            SRV_DATA->log, sizeof(dare_log_t) + SRV_DATA->log->len, 
            IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | 
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);
    if (NULL == IBDEV->lcl_mr[LOG_QP]) {
        error_return(1, log_fp, "Cannot register memory because %s\n", 
                    strerror(errno));
    }
        
        
    /* Register memory for the preregister snapshot */
    IBDEV->prereg_snapshot_mr = ibv_reg_mr(IBDEV->rc_pd, SRV_DATA->prereg_snapshot, 
            sizeof(snapshot_t) + PREREG_SNAPSHOT_SIZE, 
            IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | 
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);
    if (NULL == IBDEV->prereg_snapshot_mr) {
        error_return(1, log_fp, "Cannot register memory because %s\n", 
                    strerror(errno));
    }
    
    return 0;
}

static void
rc_memory_dereg()
{
    int rc;
    
    if (NULL != IBDEV->lcl_mr[LOG_QP]) {
        rc = ibv_dereg_mr(IBDEV->lcl_mr[LOG_QP]);
        if (0 != rc) {
            error(log_fp, "Cannot deregister memory");
        }
        IBDEV->lcl_mr[LOG_QP] = NULL;
    }
    if (NULL != IBDEV->lcl_mr[CTRL_QP]) {
        rc = ibv_dereg_mr(IBDEV->lcl_mr[CTRL_QP]);
        if (0 != rc) {
            error(log_fp, "Cannot deregister memory");
        }
        IBDEV->lcl_mr[CTRL_QP] = NULL;
    }
    if (NULL != IBDEV->prereg_snapshot_mr) {
        rc = ibv_dereg_mr(IBDEV->prereg_snapshot_mr);
        if (0 != rc) {
            error(log_fp, "Cannot deregister memory");
        }
        IBDEV->prereg_snapshot_mr = NULL;
    }
    if (NULL != IBDEV->snapshot_mr) {
        rc = ibv_dereg_mr(IBDEV->snapshot_mr);
        if (0 != rc) {
            error(log_fp, "Cannot deregister memory");
        }
        IBDEV->snapshot_mr = NULL;
    }
}

static int 
rc_qp_create( dare_ib_ep_t* ep )
{
    int i;
    struct ibv_qp_init_attr qp_init_attr;
    
    if (NULL == ep) return 0;
    
    for (i = 0; i < 2; i++) {
        /* Create RC QP */
        memset(&qp_init_attr, 0, sizeof(qp_init_attr));
        qp_init_attr.qp_type = IBV_QPT_RC;
        qp_init_attr.send_cq = IBDEV->rc_cq[i];
        qp_init_attr.recv_cq = IBDEV->rc_cq[i];
        qp_init_attr.cap.max_inline_data = IBDEV->rc_max_inline_data;
        qp_init_attr.cap.max_send_sge = 1;  
        qp_init_attr.cap.max_recv_sge = 1;
        qp_init_attr.cap.max_recv_wr = 1;
        qp_init_attr.cap.max_send_wr = IBDEV->rc_max_send_wr;
        ep->rc_ep.rc_qp[i].qp = ibv_create_qp(IBDEV->rc_pd, &qp_init_attr);
        if (NULL == ep->rc_ep.rc_qp[i].qp) {
            error_return(1, log_fp, "Cannot create QP\n");
        }
        ep->rc_ep.rc_qp[i].signaled_wr_id = 0;
        ep->rc_ep.rc_qp[i].send_count = 0;
        ep->rc_ep.rc_qp[i].state = RC_QP_ACTIVE;
        
        //rc = rc_qp_reset_to_init(ep, i);
        //if (0 != rc) {
        //    error_return(1, log_fp, "Cannot move QP to init state\n");
        //}
    }
    return 0;
}

static void 
rc_qp_destroy( dare_ib_ep_t* ep )
{
    int rc, i;
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    struct ibv_wc wc;

    if (NULL == ep) return;
    
    for (i = 0; i < 2; i++) {
        if (NULL == ep->rc_ep.rc_qp[i].qp) continue;
#if 1        
        ibv_query_qp(ep->rc_ep.rc_qp[i].qp, &attr, IBV_QP_STATE, &init_attr);
        if (attr.qp_state != IBV_QPS_RESET) {
            /* Move QP into the ERR state to cancel all outstanding WR */
            memset(&attr, 0, sizeof(attr));
            attr.qp_state = IBV_QPS_ERR;
            rc = ibv_modify_qp(ep->rc_ep.rc_qp[i].qp, &attr, IBV_QP_STATE);
            if (0 != rc) {
                error(log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
                continue;
            }
            /* Empty the corresponding CQ */
            while (ibv_poll_cq(IBDEV->rc_cq[i], 1, &wc) > 0);// info(log_fp, "while...\n");
        }
#endif        
        rc = ibv_destroy_qp(ep->rc_ep.rc_qp[i].qp);
        if (0 != rc) {
            error(log_fp, "ibv_destroy_qp failed because %s\n", strerror(rc));
        }
        ep->rc_ep.rc_qp[i].qp = NULL;
    }
}

#endif

/* ================================================================== */
/* Starting a server */
#if 1

/**
 * Get the term and index of the last given vote
 * Note: the operation succeeds only if at least a majority of servers 
 * are accessible
 */
int rc_get_replicated_vote()
{
    int rc;
    dare_ib_ep_t *ep;
    uint8_t i, size = get_group_size(SRV_DATA->config);
    int posted_sends[MAX_SERVER_COUNT];
    TIMER_INIT;
    
    /* Set remote offset */
    uint32_t offset = (uint32_t) (offsetof(ctrl_data_t, prv_data) 
                + sizeof(prv_data_t) * SRV_DATA->config.idx 
                + offsetof(prv_data_t, vote_sid));
    
    /** 
     * Post send operations
     * Note: the read operation is done through the ctrl QP
     */
    memset(posted_sends, 0, MAX_SERVER_COUNT*sizeof(int));
    ssn++;  // increase ssn to avoid past work completions
    TIMER_START(log_fp, "Get replicated vote (%"PRIu64")\n", ssn);
    for (i = 0; i < size; i++) {
        if (i == SRV_DATA->config.idx) {
            /* Cannot get vote from myself */
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
            /* Server is off */
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) {
            /* No RC data for this server */
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        text(log_fp, "   (p%"PRIu8")\n", i);
        
        rem_mem_t rm;
        rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
        posted_sends[i] = 1;
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, CTRL_QP, &SRV_DATA->ctrl_data->rsid[i],
                        sizeof(uint64_t), IBDEV->lcl_mr[CTRL_QP],
                        IBV_WR_RDMA_READ, SIGNALED, rm, posted_sends);
        if (0 != rc) {
            /* This should never happen */
            error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
        }
    }
    TIMER_STOP(log_fp);
    
    rc = wait_for_majority(posted_sends, CTRL_QP);
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot read replicated vote\n");
    }
    if (RC_SUCCESS != rc) {
        /* Operation failed; try again later */
        return -1;
    }
    
    /* Find if there is a better vote SID */
    for (i = 0; i < size; i++) {
        if (posted_sends[i] == 0) {
            /* Read operation was successful */
            if (SRV_DATA->ctrl_data->sid < SRV_DATA->ctrl_data->rsid[i]) {
                SRV_DATA->ctrl_data->sid = SRV_DATA->ctrl_data->rsid[i];
            }
            SRV_DATA->ctrl_data->rsid[i] = 0;
        }
    }
    /* Update the cache SID */
    text(log_fp, "SID after getting the replicated vote:"); 
    PRINT_SID_(SRV_DATA->ctrl_data->sid);
    
    return 0;
}

/**
 * Server recovery: Send request to get a snapshot of the SM
 */
int rc_send_sm_request()
{
    int rc;
    dare_ib_ep_t *ep;
    rem_mem_t rm;
    uint32_t offset;
    uint8_t i, size = get_group_size(SRV_DATA->config);
    TIMER_INIT;
    
    /* Send SM requests */
    uint64_t *request = &SRV_DATA->ctrl_data->sm_req[SRV_DATA->config.idx];
    *request = 1;
    offset = (uint32_t) (offsetof(ctrl_data_t, sm_req) 
            + sizeof(uint64_t) * SRV_DATA->config.idx);
    ssn++;  // increase ssn to avoid past work completions
    TIMER_START(log_fp, "Send SM requests (%"PRIu64")\n", ssn); 
    for (i = 0; i < size; i++) {
        if ( (i == SRV_DATA->config.idx) ||
            !CID_IS_SERVER_ON(SRV_DATA->config.cid, i) ) 
            continue;
            
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) {
            continue;
        }
        text(log_fp, "   (p%"PRIu8")\n", i);
        
        rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, CTRL_QP, request,
                        sizeof(uint64_t), IBDEV->lcl_mr[CTRL_QP],
                        IBV_WR_RDMA_WRITE, NOTSIGNALED, rm, NULL);
        if (0 != rc) {
            /* This should never happen */
            error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
        }
    }
    TIMER_STOP(log_fp);
    
    return 0;
}

/**
 * Server recovery: Send SM reply
 */
int rc_send_sm_reply( uint8_t target, void *s, int reg_mem )
{
    int rc;
    dare_ib_ep_t *ep;
    rem_mem_t rm;
    uint32_t offset;
    struct ibv_mr *lcl_mr;
    snapshot_t *snapshot = (snapshot_t*)s;
    
    TIMER_INIT;

    /* Set register memory */
    if (reg_mem) {
        /* Register memory for the snapshot */
        if (NULL != IBDEV->snapshot_mr) {
            /* For some reason the snapshot_mr is already registered */
            rc = ibv_dereg_mr(IBDEV->snapshot_mr);
            if (0 != rc) {
                error(log_fp, "Cannot deregister memory");
            }
        }
        IBDEV->snapshot_mr = ibv_reg_mr(IBDEV->rc_pd, snapshot, 
                sizeof(snapshot_t) + snapshot->len, 
                IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | 
                IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);
        if (NULL == IBDEV->snapshot_mr) {
            error_return(1, log_fp, "Cannot register memory");
        }
    }
    /* Set the local MR */
    if (snapshot == SRV_DATA->prereg_snapshot) {
        lcl_mr = IBDEV->prereg_snapshot_mr;
    }
    else {
        lcl_mr = IBDEV->snapshot_mr;
    }
    
    /* Send SM reply */
    sm_rep_t *reply = &SRV_DATA->ctrl_data->sm_rep[SRV_DATA->config.idx];
    reply->sid = SRV_DATA->ctrl_data->sid;
    reply->raddr = (uint64_t)snapshot;
    reply->rkey = lcl_mr->rkey;
    reply->len = sizeof(snapshot_t) + snapshot->len;
    
    offset = (uint32_t) (offsetof(ctrl_data_t, sm_rep) 
            + sizeof(sm_rep_t) * SRV_DATA->config.idx);
            
    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[target].ep;
    if (0 == ep->rc_connected) {
        return 0;
    }
    ssn++;  // increase ssn to avoid past work completions
    TIMER_START(log_fp, "Send SM reply (%"PRIu64")\n", ssn); 
    text(log_fp, "   (p%"PRIu8")\n", target);
        
    rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
    rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
    
    /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
    rc = post_send(target, CTRL_QP, reply, sizeof(sm_rep_t), 
                IBDEV->lcl_mr[CTRL_QP], IBV_WR_RDMA_WRITE, 
                NOTSIGNALED, rm, NULL);
    if (0 != rc) {
        /* This should never happen */
        error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
    }
    TIMER_STOP(log_fp);
    
    return 0;
}

/**
 * Server recovery: Get the SM of a random picked server referred to 
 * as the target. For this, the target must dump it's SM in a contiguous 
 * buffer that is accessible through RDMA. The SM contains the offset of 
 * the last applied entry; thus, lcl.apply = SM.apply
 * 
 * !!! Note: to avoid connecting the LOG QPs, we use the CTRL QP
 */
int rc_recover_sm( uint8_t target )
{
    int rc;
    rem_mem_t rm;
    uint8_t i, size = get_group_size(SRV_DATA->config);
    int posted_sends[MAX_SERVER_COUNT];
    TIMER_INIT;
       
    /* Set local memory where to store the SM snapshot */
    snapshot_t *snapshot;
    struct ibv_mr *lcl_mr;
    if (SRV_DATA->ctrl_data->sm_rep[target].len <= PREREG_SNAPSHOT_SIZE) {
        info(log_fp, "   # pre-register snapshot\n");
        snapshot = SRV_DATA->prereg_snapshot;
        lcl_mr = IBDEV->prereg_snapshot_mr;
    }
    else {
        /* Allocate memory for the snapshot */
        snapshot = SRV_DATA->snapshot;
        if (snapshot != NULL) {
            /* For some reason the snapshot is already allocated */
            free(snapshot);
        }
        rc = posix_memalign((void**)&snapshot, sizeof(uint64_t), 
            sizeof(snapshot_t) + SRV_DATA->ctrl_data->sm_rep[target].len);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot allocate snapshot\n");
        }
        /* Register memory for the snapshot */
        if (NULL != IBDEV->snapshot_mr) {
            /* For some reason the snapshot_mr is already registered */
            rc = ibv_dereg_mr(IBDEV->snapshot_mr);
            if (0 != rc) {
                error(log_fp, "Cannot deregister memory");
            }
        }
        IBDEV->snapshot_mr = ibv_reg_mr(IBDEV->rc_pd, snapshot, 
            sizeof(snapshot_t) + SRV_DATA->ctrl_data->sm_rep[target].len, 
            IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | 
            IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);
        if (NULL == IBDEV->snapshot_mr) {
            error_return(1, log_fp, "Cannot register memory");
        }
        lcl_mr = IBDEV->snapshot_mr;
    }
    
    /* Post send op only for the target */
    for (i = 0; i < size; i++) {
        posted_sends[i] = -1;
    }
    ssn++;  // increase ssn to avoid past work completions
    TIMER_START(log_fp, "Recover SM (%"PRIu64")\n", ssn); 
    text(log_fp, "   (p%"PRIu8")\n", target);    
    
    info(log_fp, "   # recover snapshot from p%"PRIu8"\n", target);    
    
    rm.raddr = SRV_DATA->ctrl_data->sm_rep[target].raddr;
    rm.rkey = SRV_DATA->ctrl_data->sm_rep[target].rkey;
    posted_sends[target] = 1;
    /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
    rc = post_send(target, CTRL_QP, snapshot,
                    SRV_DATA->ctrl_data->sm_rep[target].len, 
                    lcl_mr, IBV_WR_RDMA_READ, SIGNALED, rm, posted_sends);
    if (0 != rc) {
        /* This should never happen */
        error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
    }
    TIMER_STOP(log_fp);
    info(log_fp, "   # waiting for snapshot\n");
    
    rc = wait_for_one(posted_sends, CTRL_QP);
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot get log entries\n");
    }
    if (RC_SUCCESS != rc) {
        /* Operation failed; try again later */
        return -1;
    }
    info(log_fp, "   # snapshot recovered; apply it\n");
    
    /* Successfully recovered the snapshot - apply it */
    rc = SRV_DATA->sm->apply_snapshot(SRV_DATA->sm, snapshot->data, 
                snapshot->len);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot apply SM snapshot\n");
    }
    SRV_DATA->log->apply = snapshot->last_entry.offset;
    
    info(log_fp, "   # snapshot applied; apply = %"PRIu64"\n", SRV_DATA->log->apply);
    
    /* Free allocated memory if needed */
    if (SRV_DATA->ctrl_data->sm_rep[target].len > PREREG_SNAPSHOT_SIZE) {
        if (NULL != IBDEV->snapshot_mr) {
            rc = ibv_dereg_mr(IBDEV->snapshot_mr);
            if (0 != rc) {
                error(log_fp, "Cannot deregister memory");
            }
            IBDEV->snapshot_mr = NULL;
        }
        if (NULL != SRV_DATA->snapshot) {
            free(SRV_DATA->snapshot);
            SRV_DATA->snapshot = NULL;
        }
    }
    info(log_fp, "   # snapshot recovered\n");
    
    return 0;
}

/**
 * Server recovery:
 *  - Get the commit and the end offsets from a server; 
 * lcl.head <= any rmt.apply <= any rmt.commit; 
 * also, lcl.head < any rmt.end; thus, to 
 * be sure that we get at least an entry from the log, we need to read 
 * both the commit and the end offsets
 *  - Get (from the target) the log entries between the head and 
 *    the end offsets.
 *  - Set end offset to the commit offset: lcl.end = lcl.commit
 * Note: lcl.head was set when the server receives a reply for the JOIN req
 * 
 * !!! Note: to avoid connecting the LOG QPs, we use the CTRL QP
 */
int rc_recover_log()
{
    int rc;
    dare_ib_ep_t *ep;
    rem_mem_t rm;
    uint32_t offset, len;
    uint8_t i, size = get_group_size(SRV_DATA->config);
    uint8_t target;
    int posted_sends[MAX_SERVER_COUNT];
    TIMER_INIT;

    /**
     * Get the commit and end offset from a server
     */
    offset = (uint32_t) (offsetof(dare_log_t, commit));
    memset(posted_sends, 0, MAX_SERVER_COUNT*sizeof(int));
    ssn++;  // increase ssn to avoid past work completions
    TIMER_START(log_fp, "Get remote commit, end offsets (%"PRIu64")\n", ssn); 
    for (i = 0; i < size; i++) {
        if ( (i == SRV_DATA->config.idx) || 
            !CID_IS_SERVER_ON(SRV_DATA->config.cid, i) )
        {
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) {
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        text(log_fp, "   (p%"PRIu8")\n", i);
        
        rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;
        posted_sends[i] = 1;
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, CTRL_QP, 
                        &SRV_DATA->ctrl_data->log_offsets[i].commit,
                        2*sizeof(uint64_t), IBDEV->lcl_mr[CTRL_QP],
                        IBV_WR_RDMA_READ, SIGNALED, rm, posted_sends);
        if (0 != rc) {
            /* This should never happen */
            error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
        }
    }
    TIMER_STOP(log_fp);
    
    /* Wait just for one since any one will do */
    rc = wait_for_one(posted_sends, CTRL_QP);
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot read log info\n");
    }
    if (RC_SUCCESS != rc) {
        /* Operation failed; try again later */
        return -1;
    }
    
    /* Find the target, i.e., one successful operation */
    for (target = 0; target < size; target++) {
        if (0 == posted_sends[target]) {
            break;
        }
    }

    log_offsets_t *rmt_offsets = &SRV_DATA->ctrl_data->log_offsets[target];
    if (rmt_offsets->end == SRV_DATA->log->len)
    {
        /* The remote log is empty; note that note that this can 
        happen only at start up (head = 0), since log pruning leaves 
        the last appended entry in the log */
        return 0;
    }
    
    /* Remote log is not empty */
    SRV_DATA->log->end = rmt_offsets->end;
    if ((rmt_offsets->end > 0) &&
        (rmt_offsets->end < SRV_DATA->log->head)) {
        /* The log entries wrap around; not necessary to get all entries; 
        the leader will handle the rest (log update) */
        rmt_offsets->end = 0;
    }
    if (log_is_offset_larger(SRV_DATA->log, 
            rmt_offsets->commit, rmt_offsets->end))
    {
        rmt_offsets->commit = rmt_offsets->end;
    }
    SRV_DATA->log->end = rmt_offsets->end;
    SRV_DATA->log->commit = rmt_offsets->commit;
    
    /* Get the log entries between the head and the end offsets */
    offset = (uint32_t)(offsetof(dare_log_t, entries) 
            + SRV_DATA->log->head);
    len = log_offset_end_distance(SRV_DATA->log, SRV_DATA->log->head);
    info(log_fp, "Recovering log entries (len = %"PRIu32" bytes)\n", len);
    
    /* Post send just for the target */
    for (i = 0; i < size; i++) {
        posted_sends[i] = -1;
    }
    ssn++;  // increase ssn to avoid past work completions
    TIMER_START(log_fp, "Recover LOG (%"PRIu64")\n", ssn); 
    text(log_fp, "   (p%"PRIu8")\n", target);    
    
    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[target].ep;
    rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
    rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;
    posted_sends[target] = 1;
    /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
    rc = post_send(target, CTRL_QP, SRV_DATA->log->entries + SRV_DATA->log->head,
                    len, IBDEV->lcl_mr[LOG_QP],
                    IBV_WR_RDMA_READ, SIGNALED, rm, posted_sends);
    if (0 != rc) {
        /* This should never happen */
        error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
    }
    TIMER_STOP(log_fp);
    
    rc = wait_for_one(posted_sends, CTRL_QP);
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot get log entries\n");
    }
    if (RC_SUCCESS != rc) {
        /* Operation failed; try again later */
        return -1;
    }
             
    return 0;
}

#endif

/* ================================================================== */
/* HB mechanism */
#if 1

/**
 * Send HB over RDMA (HB = cached SID)
 * Only leaders and candidates do this
 */
int rc_send_hb()
{
    int rc;
    dare_ib_ep_t *ep;
    uint8_t i, size;
    
    TIMER_INIT;
    
    /* No need to send HBs to servers in the extended config */
    size = get_group_size(SRV_DATA->config);
    
    /* Set offset accordingly */
    uint32_t offset = (uint32_t) (offsetof(ctrl_data_t, hb) 
                    + sizeof(uint64_t) * SRV_DATA->config.idx);
    
    /* Issue RDMA Write operations */
    ssn++;
    //TIMER_START(log_fp, "Sending HB (%"PRIu64")\n", ssn);
    for (i = 0; i < size; i++) {
        if (i == SRV_DATA->config.idx) continue;
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
            continue;
        }
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) {
            continue;
        }
        //text(log_fp, "   (p%"PRIu8")\n", i);
        
        rem_mem_t rm;
        rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, CTRL_QP, &SRV_DATA->ctrl_data->sid,
                        sizeof(uint64_t), IBDEV->lcl_mr[CTRL_QP],
                        IBV_WR_RDMA_WRITE, NOTSIGNALED, rm, NULL);
        if (0 != rc) {
            /* This should never happen */
            error_return(1, log_fp, "Cannot post send operation\n");
        }
    }
    //TIMER_STOP(log_fp);

    return 0;
}

/**
 * Send HB Reply over RDMA (HB = cached SID)
 * Done when receiving an outdated HB
 */
int rc_send_hb_reply( uint8_t idx )
{
    int rc;
    TIMER_INIT;
    
    if (idx >= get_group_size(SRV_DATA->config)) 
        error_return(1, log_fp, "Index out of bound\n");
    
    /* Set offset accordingly */
    uint32_t offset = (uint32_t) (offsetof(ctrl_data_t, hb) 
                    + sizeof(uint64_t) * SRV_DATA->config.idx);
    
    /* Issue RDMA Write operations */
    ssn++;
    TIMER_START(log_fp, "Sending HB reply (%"PRIu64")\n", ssn);
    
    if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, idx)) {
        return 0;
    }
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[idx].ep;
    if (0 == ep->rc_connected) {
        return 0;
    }
    text(log_fp, "   (p%"PRIu8")\n", idx);
        
    rem_mem_t rm;
    rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
    rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
    
    /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
    rc = post_send(idx, CTRL_QP, &SRV_DATA->ctrl_data->sid,
                    sizeof(uint64_t), IBDEV->lcl_mr[CTRL_QP],
                    IBV_WR_RDMA_WRITE, NOTSIGNALED, rm, NULL);
    if (0 != rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot post send operation\n");
    }
    TIMER_STOP(log_fp);

    return 0;
}

#endif

/* ================================================================== */
/* Leader election */
#if 1
/**
 * Send vote requests to the other servers; that is,
 * write SID, index & term of last log entry
 */
int rc_send_vote_request()
{
    int rc;
    uint8_t i, size = get_group_size(SRV_DATA->config);
    uint8_t idx = SRV_DATA->config.idx;
    dare_ib_ep_t *ep;
    rem_mem_t rm;
    //int posted_sends[MAX_SERVER_COUNT];
    TIMER_INIT;
    
    /* Set vote request */
    vote_req_t *request = &(SRV_DATA->ctrl_data->vote_req[idx]);
    request->sid = SRV_DATA->ctrl_data->sid;
    if (is_log_empty(SRV_DATA->log)) {
        /* The log is empty */
        request->index = 0;
        request->term = 0;
    }
    else {
        uint64_t tail = log_get_tail(SRV_DATA->log);
        dare_log_entry_t* last_entry = 
            log_get_entry(SRV_DATA->log, &tail);
        request->index = last_entry->idx;
        request->term = last_entry->term;
    }
    request->cid = SRV_DATA->config.cid;
    //debug(log_fp, "sending vote request L.E.: idx=%"PRIu64"; term=%"PRIu64"\n", 
    //        request->index, request->term);

    /* Set remote offset */
    uint32_t offset = (uint32_t) (offsetof(ctrl_data_t, vote_req) 
                    + sizeof(vote_req_t) * idx);
    
    /* Issue RDMA Write operations */
    ssn++;
    //memset(posted_sends, 0, MAX_SERVER_COUNT*sizeof(int));
    TIMER_START(log_fp, "Sending vote request (%"PRIu64")\n", ssn);
    for (i = 0; i < size; i++) {
        if (idx == i) continue;
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
            //posted_sends[i] = -1;  // insuccess
            continue;
        }
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) {
            //posted_sends[i] = -1;  // insuccess
            continue;
        }
        text(log_fp, "   (p%"PRIu8")\n", i);
        
        /* Set address and key of remote memory region */
        rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
        
        //posted_sends[i] = 1;
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, CTRL_QP, request, sizeof(vote_req_t), 
                        IBDEV->lcl_mr[CTRL_QP], IBV_WR_RDMA_WRITE, 
                        NOTSIGNALED, rm, NULL);
        if (0 != rc) {
            /* This should never happen */
            error_return(1, log_fp, "Cannot post send operation\n");
        }
        //debug(log_fp, "Sent vote request to %"PRIu8"\n", i);
    }
    TIMER_STOP(log_fp);
    /* Wait for a majority of sends to complete */
    //rc = wait_for_majority(posted_sends, CTRL_QP);
    //if (0 != rc) {
        /* This should never happen */
    //    error_return(1, log_fp, "Did not receive completion events\n");
    //}

    return 0;
}

/**
 * Replicate SID of a the candidate that receives my vote;
 * future SIDs of this index cannot be lower than the replicated SID
 */
int rc_replicate_vote()
{
    int rc;
    dare_ib_ep_t *ep;
    rem_mem_t rm;
    uint8_t i, size = get_group_size(SRV_DATA->config);
    uint8_t idx = SRV_DATA->config.idx;
    int posted_sends[MAX_SERVER_COUNT];
    memset(posted_sends, 0, MAX_SERVER_COUNT*sizeof(int));
    TIMER_INIT;

    /* Set vote_sid in the private data */
    uint64_t *vsid = &SRV_DATA->ctrl_data->prv_data[idx].vote_sid;
    *vsid = SRV_DATA->ctrl_data->sid;
    
    /* Compute offset for RDMA write */
    uint32_t offset = (uint32_t) (offsetof(ctrl_data_t, prv_data) 
                    + sizeof(prv_data_t) * idx 
                    + offsetof(prv_data_t, vote_sid));
    
    /* Issue RDMA Write operations */
    ssn++;  // increase ssn to avoid past work completions
    TIMER_START(log_fp, "Replicating voting SID (%"PRIu64")\n", ssn);
    for (i = 0; i < size; i++) {
        if (idx == i) continue;
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) {
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        text(log_fp, "   (p%"PRIu8")\n", i);
        
        /* Set address and key of remote memory region */
        rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
        
        posted_sends[i] = 1;
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, CTRL_QP, vsid, sizeof(uint64_t), 
                        IBDEV->lcl_mr[CTRL_QP], IBV_WR_RDMA_WRITE, 
                        SIGNALED, rm, posted_sends);          
        if (0 != rc) {
            /* This should never happen */
            error_return(1, log_fp, "Cannot post send operation\n");
        }
    }
    TIMER_STOP(log_fp);
    
    /* Wait for a majority of sends to complete */
    rc = wait_for_majority(posted_sends, CTRL_QP);
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Did not receive completion events\n");
    }
    
    return 0;
}

/**
 * Send vote ACK to the candidate
 * The ACK is the local commit offset 
 * Note: the ack may or may not reach the destination
 */
int rc_send_vote_ack()
{
    int rc, i;
    dare_ib_ep_t *ep;
    rem_mem_t rm;
    uint8_t candidate = SID_GET_IDX(SRV_DATA->ctrl_data->sid);
    uint8_t idx = SRV_DATA->config.idx;
    int posted_sends[MAX_SERVER_COUNT];

    /* Get remote endpoint */
    if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, candidate)) {
        return 0;
    }
    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[candidate].ep;          
    if (0 == ep->rc_connected) {
        return 0;
    }
       
    /* Set remote offset */
    uint32_t offset = (uint32_t)(offsetof(ctrl_data_t, vote_ack) + 
                            sizeof(uint64_t) * idx) ;
                            
    /* Set address and key of remote memory region */
    rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
    rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
    
    /* Post send just for the candidate */
    for (i = 0; i < MAX_SERVER_COUNT; i++) {
        posted_sends[i] = -1;
    }
    posted_sends[candidate] = 1;
    ssn++;
    text(log_fp, "Send vote ACK to p%"PRIu8" (%"PRIu64")\n", candidate, ssn);
    /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
    rc = post_send(candidate, CTRL_QP, &SRV_DATA->log->commit, 
                    sizeof(uint64_t), IBDEV->lcl_mr[LOG_QP], 
                    IBV_WR_RDMA_WRITE, SIGNALED, rm, posted_sends); 
    if (0 != rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot post send operation\n");
    }
    
    /* Make sure the candidate gets the ACK; if not, then probably it failed */
    rc = wait_for_one(posted_sends, CTRL_QP);
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot send vote ACK\n");
    }
    if (RC_SUCCESS != rc) {
        /* Operation failed; thus, the candidate has probably failed */
        return -1;
    }

    return 0;
}

#endif

/* ================================================================== */
/* Normal operation */
#if 1

/**
 * Read the remote SID and see if there is a better leader than me
 * Needed for client read requests
 */
int rc_verify_leadership( int *leader )
{
    int rc;
    dare_ib_ep_t *ep;
    rem_mem_t rm;
    uint64_t sid;
    uint8_t i, size = get_group_size(SRV_DATA->config);
    int posted_sends[MAX_SERVER_COUNT];
    TIMER_INIT;
    
    /* Set remote offset */
    uint32_t offset = (uint32_t) (offsetof(ctrl_data_t, sid));
    //HRT_GET_TIMESTAMP(SRV_DATA->t1);
        
    /** 
     * Read SID of remote servers 
     */
    memset(posted_sends, 0, MAX_SERVER_COUNT*sizeof(int));
    ssn++;  // increase ssn to avoid past work completions
    TIMER_START(log_fp, "Verify leadership (%"PRIu64")\n", ssn);
    for (i = 0; i < size; i++) {
        if (i == SRV_DATA->config.idx) {
            continue;
        }
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) {
            /* No RC data for this endpoint */
            posted_sends[i] = -1;  // insuccess
            continue;
        }
        text(log_fp, "   (p%"PRIu8")\n", i);
        
        /* Set address and key of remote memory region */
        rm.raddr = ep->rc_ep.rmt_mr[CTRL_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[CTRL_QP].rkey;
        
        posted_sends[i] = 1;
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, CTRL_QP, &SRV_DATA->ctrl_data->rsid[i], 
                        sizeof(uint64_t), IBDEV->lcl_mr[CTRL_QP], 
                        IBV_WR_RDMA_READ, SIGNALED, rm, posted_sends); 
        if (0 != rc) {
            /* This should never happen */
            error_return(1, log_fp, "Cannot post send operation\n");
        }
    }
    TIMER_STOP(log_fp);
    //HRT_GET_TIMESTAMP(SRV_DATA->t2);
    /* Wait for a majority of sends to complete */
    rc = wait_for_majority(posted_sends, CTRL_QP);
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Did not receive completion events\n");
    }   
    
    //HRT_GET_TIMESTAMP(SRV_DATA->t2);
    /**
     * Check remote SIDs to see if there is a better one 
     */
    uint64_t new_sid = SRV_DATA->ctrl_data->sid;   
    for (i = 0; i < size; i++) {
        if (i == SRV_DATA->config.idx) {
            continue;
        }
        if (0 != posted_sends[i]) {
            /* The read operation was unsuccessful */
            continue;
        }
        /* The term and index of the remote server */
        sid = SRV_DATA->ctrl_data->rsid[i];
        if (sid > new_sid) {
            /* The remote server has a higher term; check though if it 
             * is aware of a leader*/
            if (SID_GET_L(sid)) {
                new_sid = sid;
                continue;
            }
        }
    }
    if (new_sid != SRV_DATA->ctrl_data->sid) {
        /* A better leader was found */
        *leader = 0;
        rc = server_update_sid(new_sid, SRV_DATA->ctrl_data->sid);
        if (0 != rc) {
            /* Cannot update SID - go back to follower state */
            return 0;
        }
        /* SID modified: go to follower state */
        server_to_follower();
        return 0;
    }
    *leader = 1;
    
    return 0;
}

/**
 * Log adjustment phase
 *  - read the remote commit offset (note that if the remote server has 
 * no not committed entries, we need to set the end offset to the 
 * remote commit offset)
 *  - read the number of not committed entries
 *  - read the not committed entries
 *  - find offset of first non-matching entry and update remote end offset
 * Note: only the leader calls this function
 */
static int
log_adjustment()
{
    int rc, init;
    server_t *server;
    dare_ib_ep_t *ep;
    void *local_buf;
    uint32_t local_buf_len;
    struct ibv_mr *local_mr;
    enum ibv_wr_opcode rdma_opcode;
    rem_mem_t rm;        
    uint8_t i, size;
    uint32_t offset;
    uint64_t remote_commit, *remote_end;
    dare_nc_buf_t *nc_buf;
    TIMER_INIT;
    
    /* Adjust the log of all servers; 
    including the ones in the extended configuration;
    Note: the leader cannot access the logs of not-recovered servers 
    and all log adjustment operations are going through the LOG QP */
    size = get_extended_group_size(SRV_DATA->config);
    for (i = 0, init = 0; i < size; i++) {
        if ( (i == SRV_DATA->config.idx) ||
            !CID_IS_SERVER_ON(SRV_DATA->config.cid, i) )
            continue;
        
        /* Server is on and it's not me */
        server = &SRV_DATA->config.servers[i];
        if (server->fail_count >= PERMANENT_FAILURE) {
            /* The leader suspects this server */
            continue;
        }
        if (!server->send_flag) {
            /* Already waiting for a WR on this QP */
            continue;
        }

        ep = (dare_ib_ep_t*)server->ep;
        if (0 == ep->rc_connected) {
            /* No RC data for this endpoint */
            continue;
        }
        remote_commit = SRV_DATA->ctrl_data->vote_ack[i];
        if (SRV_DATA->log->len == remote_commit) {
            /* No vote ACK from this server */
            continue;
        }
        
        if ( (!init) && (server->next_lr_step < LR_UPDATE_LOG) ) {
            ssn++;  // increase ssn to avoid past work completions
            TIMER_START(log_fp, "### Log adjustment (%"PRIu64")\n", ssn);
            init = 1;
        }
        /* Check in what state of log adjustment is this server */
        switch(server->next_lr_step) {
            case LR_GET_WRITE: 
            { /* Step I: Read the remote commit offset --
                 No need to get the commit offset; it comes with the vote ACK */
                SRV_DATA->ctrl_data->log_offsets[i].commit = remote_commit;
                server->next_lr_step = LR_GET_NCE_LEN;
            }
            case LR_GET_NCE_LEN:
            { /* Step II.a): Read the number of not committed entries */
                /* Note: optimization where the servers create a buffer 
                 * with not committed entries
                 * Note: if we can access the remote logs, we can be sure 
                 * that the remote side already published the buffer with 
                 * non committed entries */
                TIMER_INFO(log_fp, "   (p%"PRIu8": get #not-committed entries)\n", i);
                /* Check if the leader should update the local commit offset */
                if (log_is_offset_larger(SRV_DATA->log, remote_commit,
                                             SRV_DATA->log->commit))
                {
                    /* Update local commit */
                    SRV_DATA->log->commit = remote_commit;
                }
                /* Set remote offset */
                offset = (uint32_t) (offsetof(dare_log_t, nc_buf) 
                            + sizeof(dare_nc_buf_t) * i 
                            + offsetof(dare_nc_buf_t, len));
                /* Set send fields */
                local_buf = &SRV_DATA->log->nc_buf[i].len;
                local_buf_len = sizeof(uint64_t);
                local_mr = IBDEV->lcl_mr[LOG_QP];;
                rdma_opcode = IBV_WR_RDMA_READ;
                break;
            }
            case LR_GET_NCE:
            { /*Step II.b): Read the not committed entries */
                nc_buf = &SRV_DATA->log->nc_buf[i];
                if (0 == nc_buf->len) {
                    /* This server has no not committed entries; 
                     * log adjustment done */
                    SRV_DATA->ctrl_data->log_offsets[i].end = 
                                SRV_DATA->ctrl_data->log_offsets[i].commit;
                    server->next_lr_step = LR_UPDATE_LOG;
                    TIMER_INFO(log_fp, "   (p%"PRIu8
                            ": all remote entries are committed)\n", i);
                    continue;
                }
                TIMER_INFO(log_fp, "   (p%"PRIu8": get %"PRIu64
                            " not committed entries)\n", i, nc_buf->len);
                /* Set remote offset */
                offset = (uint32_t) (offsetof(dare_log_t, nc_buf) 
                            + sizeof(dare_nc_buf_t) * i 
                            + offsetof(dare_nc_buf_t, entries));
                /* Set send fields */
                local_buf = nc_buf->entries;
                local_buf_len = nc_buf->len * sizeof(dare_log_entry_det_t);
                local_mr = IBDEV->lcl_mr[LOG_QP];;
                rdma_opcode = IBV_WR_RDMA_READ;
                break;
            }
            case LR_SET_END:
            { /* Step III: Adjust remote end offset */
                /* Set remote offset */
                offset = (uint32_t) (offsetof(dare_log_t, end));
                /* Find last matching entry */
                remote_end = &SRV_DATA->ctrl_data->log_offsets[i].end;
                *remote_end = log_find_remote_end_offset(SRV_DATA->log, 
                                            &SRV_DATA->log->nc_buf[i]);
                TIMER_INFO(log_fp, "   (p%"PRIu8
                    ": set end offset to %"PRIu64")\n", i, *remote_end);
                /* Set send fields */
                local_buf = remote_end;
                local_buf_len = sizeof(uint64_t);
                local_mr = IBDEV->lcl_mr[CTRL_QP];;
                rdma_opcode = IBV_WR_RDMA_WRITE;
                break;
            }
            default:
            {
                continue;
            }
        }
        /* Set address and key of remote memory region */
        rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;
        
        /* Stop posting sends until a WC is received */
        server->send_flag = 0;
        /* Wait for the completion of the following work request */
        WRID_SET_SSN(server->next_wr_id, ssn);
        WRID_SET_CONN(server->next_wr_id, i);
        
        /* Post send operation */
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, LOG_QP, local_buf, local_buf_len, local_mr, 
                        rdma_opcode, SIGNALED, rm, NULL); 
        if (0 != rc) {
            /* This should never happen */
            error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
        }
    }
    if (init) {
        TIMER_STOP(log_fp);
    }
    return 0;
}

/**
 * Update remote logs that are cleaned up
 *  - write log entries starting with the remote end offset until the 
 * local end offset
 *  - update remote end offset
 * Note: only the leader calls this function
 */
uint64_t wr_rm_log_cnt;
int committed;
char posted_sends_str[512];

uint64_t offsets[MAX_SERVER_COUNT];
static int
update_remote_logs()
{
    int rc, init;
    server_t *server;
    dare_ib_ep_t *ep;
    void *local_buf[2];
    uint32_t local_buf_len[2];
    struct ibv_mr *local_mr;
    rem_mem_t rm;
    register uint8_t i, size;
    uint32_t offset;
    uint64_t *remote_end, *remote_commit;
    TIMER_INIT;

    //int posted_sends[MAX_SERVER_COUNT];
    //for (i = 0; i < MAX_SERVER_COUNT; i++) {
    //    posted_sends[i] = -1;
    //}
    
    /* Update the log of all servers; 
    including the ones in the extended configuration;
    Note: the leader cannot access the logs of not-recovered servers 
    and all log update operations are going through the LOG QP */
    size = get_extended_group_size(SRV_DATA->config);
//info(log_fp, "%s:%d size=%d\n", __func__, __LINE__, size);
//HRT_GET_TIMESTAMP(SRV_DATA->t1);
    for (i = 0, init = 0; i < size; i++) {
        if ( (i == SRV_DATA->config.idx) ||
            !CID_IS_SERVER_ON(SRV_DATA->config.cid, i) )
            continue;
        
        /* Server is on and it's not me */
        server = &SRV_DATA->config.servers[i];
        ep = (dare_ib_ep_t*)server->ep;
        if ( (server->fail_count >= PERMANENT_FAILURE) 
                || !server->send_flag
                || (0 == ep->rc_connected) ) {
            /* The leader suspects this server */
            continue;
        }
        /* Check in what state of log adjustment is this server */
        if (LR_UPDATE_LOG == server->next_lr_step)  { 
            /* Step I: Update remote logs */
            /* Check if the server requires a log update */
            remote_end = &SRV_DATA->ctrl_data->log_offsets[i].end;
            if (0 == log_offset_end_distance(SRV_DATA->log, *remote_end)) {
                /* Remote server has all entries */
                continue;
            }
            if (!init) {
                ssn++;  // increase ssn to avoid past work completions
                TIMER_START(log_fp, "### Log update (%"PRIu64")\n", ssn);
                //info_wtime(log_fp, "### Log update (%"PRIu64")\n", ssn);
                init = 1;
            }
            TIMER_INFO(log_fp, "   (p%"PRIu8": write log[%"PRIu64":%"PRIu64"])\n", 
                        i, *remote_end, SRV_DATA->log->end);
            //info(log_fp, "   # (p%"PRIu8": write log[%"PRIu64":%"PRIu64"])\n", 
            //        i, *remote_end, SRV_DATA->log->end);
            /* Set remote offset */
            offset = (uint32_t)(offsetof(dare_log_t, entries) + *remote_end);
                
            /* Store the leader's current end offset; the remote end offset 
             * is set to this value after the update completes successfully */
            server->cached_end_offset = SRV_DATA->log->end;
            /* Set send fields */
            if (SRV_DATA->log->end > *remote_end) {
                local_buf[0] = SRV_DATA->log->entries + *remote_end;
                local_buf_len[0] = SRV_DATA->log->end - *remote_end;
                local_buf[1] = NULL;
                local_buf_len[1] = 0;
                server->send_count = 1;
            }
            else {
                local_buf[0] = SRV_DATA->log->entries + *remote_end;
                local_buf_len[0] = SRV_DATA->log->len - *remote_end;
                local_buf[1] = SRV_DATA->log->entries;
                local_buf_len[1] = SRV_DATA->log->end;
                server->send_count = 2;
            }
            /* Set send fields */
            local_mr = IBDEV->lcl_mr[LOG_QP];
        }
        else if (LR_UPDATE_END == server->next_lr_step)  { 
            /* Step II: Update the remote end offsets */
            if (!init) {
                ssn++;  // increase ssn to avoid past work completions
                TIMER_START(log_fp, "### Log update (%"PRIu64")\n", ssn);
                init = 1;
//HRT_GET_TIMESTAMP(SRV_DATA->t1);
//HRT_GET_TIMESTAMP(SRV_DATA->t2);
            }
            /* Set remote offset */
            offset = (uint32_t) (offsetof(dare_log_t, end));
            /* Set the remote end offset to the cached end offset */
            remote_end = &SRV_DATA->ctrl_data->log_offsets[i].end;
            *remote_end = server->cached_end_offset;
            TIMER_INFO(log_fp, "   (p%"PRIu8".end=%"PRIu64")\n", i, *remote_end);
            //info(log_fp, "   # (p%"PRIu8
            //    ": update end offset to %"PRIu64")\n", i, *remote_end);
            /* Set send fields */
            local_buf[0] = remote_end;
            local_buf_len[0] = sizeof(uint64_t);
            local_buf[1] = NULL;
            local_buf_len[1] = 0;
            /* Set send fields */
            local_mr = IBDEV->lcl_mr[CTRL_QP];
        }
        else {
            continue;
        }

        /* Set address and key of remote memory region */
        rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;
        
        /* Stop posting sends until a WC is received */
        server->send_flag = 0;
        /* Wait for the completion of the following work request */
        WRID_SET_SSN(server->next_wr_id, ssn);
        WRID_SET_CONN(server->next_wr_id, i);
        
        /* Post send operation */
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        //info(log_fp, "Posting send for p%"PRIu8" %"PRIu32" bytes\n", i, local_buf_len[0]);
//if (server->next_lr_step != LR_UPDATE_END) {
//    HRT_GET_TIMESTAMP(SRV_DATA->t1);
//}
//posted_sends[i] = 1;
#if 0
if (server->next_lr_step == LR_UPDATE_END) {
    sprintf(posted_sends_str, "%s %d-end", posted_sends_str, i);
}
else {
    sprintf(posted_sends_str, "%s %d-log", posted_sends_str, i);
}
#endif
#ifdef DEBUG
        rc = 
#endif        
        post_send(i, LOG_QP, local_buf[0], local_buf_len[0], 
            local_mr, IBV_WR_RDMA_WRITE, SIGNALED, rm, NULL); 
//post_send(i, LOG_QP, local_buf[0], local_buf_len[0], 
//      local_mr, rdma_opcode, SIGNALED, rm, posted_sends); 
#ifdef DEBUG
        if (0 != rc) {
            /* This should never happen */
            error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
        }
#endif        
//wait_for_one(posted_sends, LOG_QP);
//posted_sends[i] = -1;

        if (local_buf_len[1] > 0) {
            /* Set remote starting offset */
            offset = (uint32_t)(offsetof(dare_log_t, entries));                            
            rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
            /* Stop posting sends until a WC is received */
            server->send_flag = 0;
            /* Set Wrap-Around flag */
            wa_flag = 1;
            
            /* Post send operation */
            /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
            //info(log_fp, "!!! Posting send for p%"PRIu8" %"PRIu32" bytes\n", i, local_buf_len[1]);
#ifndef DEBUG            
            rc = 
#endif            
            post_send(i, LOG_QP, local_buf[1], local_buf_len[1], 
                    local_mr, IBV_WR_RDMA_WRITE, SIGNALED, rm, NULL);
#ifndef DEBUG            
            if (0 != rc) {
                /* This should never happen */
                error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
            }
#endif            
        } 
    }
    if (init) {
        TIMER_STOP(log_fp);
    }
//HRT_GET_TIMESTAMP(SRV_DATA->t2);


    /* Find minimum offset that exists on a majority of servers 
     * a.k.a find committed entries */
    uint64_t min_offset = SRV_DATA->log->commit;
    int j = 0;
    while(j < 2) {
        int larger_offset_count = 0;
        size = SRV_DATA->config.cid.size[j];
        /* Find the offset of the last valid entry for all servers */
        for (i = 0; i < size; i++) {
            if (i == SRV_DATA->config.idx) {
                offsets[i] = SRV_DATA->log->end;
                continue;
            }
            if ( !CID_IS_SERVER_ON(SRV_DATA->config.cid, i) || 
                (SRV_DATA->config.servers[i].fail_count >= PERMANENT_FAILURE) ||
                (SRV_DATA->config.servers[i].next_lr_step != LR_UPDATE_LOG) )
            {
                offsets[i] = SRV_DATA->log->commit;
                continue;
            }
            /* Log entries up to the end offset of this server are valid */
            offsets[i] = SRV_DATA->ctrl_data->log_offsets[i].end;
            if (log_is_offset_larger(SRV_DATA->log, offsets[i], min_offset)) {
                larger_offset_count++;
            }
        }
        if (larger_offset_count < size / 2) {
            if (CID_TRANSIT != SRV_DATA->config.cid.state) 
                break;
            if (!j)  {
                j++; continue;
            }
            break;
        }
#if 1
        //sprintf(posted_sends_str, "%s sort", posted_sends_str);
        /* Sort offsets in ascending order */
        uint64_t tmp; int k;
        for (i = 1; i < size; i++) {
            tmp = offsets[i];
            k = i;
            while ((k > 0) && (offsets[k-1] > tmp)) {
                offsets[k] = offsets[k-1];
                k--;
            }
            offsets[k] = tmp;
        }
#else        
        qsort(offsets, size, sizeof(uint64_t), cmpfunc_offset);
#endif        
#if 0        
char buf[256] = "end offsets: ";
for (i = 0; i < size; i++) {
    sprintf(buf, "%s %"PRIu64" ", buf, offsets[i]);
}
info(log_fp, "%s\n", buf);
#endif
        
        /* Note: for transitional configurations, we need to keep 
        the minimum offset over both majorities */
        if (CID_TRANSIT != SRV_DATA->config.cid.state) {
            min_offset = offsets[(size-1)/2];
            break;
        }
        else {
            /* Transitional configuration */
            uint64_t median = offsets[(size-1)/2];
            if (!j)  min_offset = median;
            else if (log_is_offset_larger(SRV_DATA->log, min_offset, median))
            //else if (min_offset > offsets[(size-1)/2]) 
                min_offset = offsets[(size-1)/2]; 
        }
        j++;
    }

    if (log_is_offset_larger(SRV_DATA->log, min_offset, SRV_DATA->log->commit)) {
    //if (SRV_DATA->log->commit < min_offset) {
        /* Update local commit offset... */ 
        SRV_DATA->log->commit = min_offset;
//uint64_t ticks;
//HRT_GET_ELAPSED_TICKS(SRV_DATA->t1, SRV_DATA->t2, &ticks);
//info(log_fp, "Write time: %lf; wr_rm_log_cnt=%"PRIu64"\n", 
//    HRT_GET_USEC(ticks), wr_rm_log_cnt);        

        /* ... also, update the local CID offset */
        SRV_DATA->config.cid_offset = SRV_DATA->log->commit;

        /* And set the commit flag */
        committed = 1;
    }
    
    /* Try to update the commit offsets */
    for (init = 0, i = 0; i < size; i++) {
        if ( (i == SRV_DATA->config.idx) ||
            !CID_IS_SERVER_ON(SRV_DATA->config.cid, i) )
            continue;
        
        /* Server is on and it's not me */
        server = &SRV_DATA->config.servers[i];
        ep = (dare_ib_ep_t*)server->ep;
        if ( (server->fail_count >= PERMANENT_FAILURE) 
                /* || !server->send_flag */
                || (0 == ep->rc_connected) 
                || (server->next_lr_step != LR_UPDATE_LOG) ) 
        {
            continue;
        }
        remote_commit = &SRV_DATA->ctrl_data->log_offsets[i].commit;
        remote_end = &SRV_DATA->ctrl_data->log_offsets[i].end;
        if ( (*remote_commit == *remote_end) ||  /* No new log entries on this server */
            (*remote_commit == SRV_DATA->log->commit) ) /* Remote commit offset is up to date */
        {
            continue;
        }
        *remote_commit = SRV_DATA->log->commit;
        if (log_is_offset_larger(SRV_DATA->log, *remote_commit, *remote_end)) {
            /* The remote log does not contain all the committed entries */
            *remote_commit = *remote_end;
        }
        if (!init) {
            ssn++;  // increase ssn to avoid past work completions
            TIMER_START(log_fp, "Lazily update commit offsets (%"PRIu64
                        ")\n", ssn);
            offset = (uint32_t) (offsetof(dare_log_t, commit));
            init = 1;
        }
        TIMER_INFO(log_fp, "   (p%"PRIu8".commit=%"PRIu64")\n", 
                    i, *remote_commit);
        
        /* Set remote offset */
        rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;
        
        /* Post send operation */
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
#if 0        
sprintf(posted_sends_str, "%s %d-wr", posted_sends_str, i);
#endif
#ifdef DEBUG 
        rc = 
#endif        
        post_send(i, LOG_QP, remote_commit, sizeof(uint64_t), 
                        IBDEV->lcl_mr[CTRL_QP], IBV_WR_RDMA_WRITE, 
                        NOTSIGNALED, rm, NULL); 
#ifdef DEBUG        
        if (0 != rc) {
            /* This should never happen */
            error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
        }
#endif        
    }
    if (init) {
        TIMER_STOP(log_fp);
    }
//HRT_GET_TIMESTAMP(SRV_DATA->t2);
    
    return RC_SUCCESS;
}

/**
 * Log replication
 */
uint64_t wrl_count_array[1000];
int wrl_idx;
int rc_write_remote_logs( int wait_for_commit )
{
    int rc;
    int threshold = 0;
    uint64_t ticks;

    if (wait_for_commit) {
        committed = 0;
        wrl_count_array[wrl_idx]=0;
        //HRT_GET_TIMESTAMP(SRV_DATA->t1);
    }

/* Horrible hack to avoid going back through libev before the commit is over */
loop_for_commit:
    if (wait_for_commit) {
        wrl_count_array[wrl_idx]++;
        /* Cannot wait until an HB is send to find WCs; thus, let's empty 
         * the LOG CQ */
#ifdef DEBUG    
        rc = 
#endif    
        empty_completion_queue(0, LOG_QP, 0, NULL);
#ifdef DEBUG        
        if (0 != rc) {
            error_return(1, log_fp, "Cannot empty completion queue\n");
        }
#endif        
    }
    threshold ++;
    /* Phase 1: Adjust remote logs */
#ifdef DEBUG    
    rc = 
#endif    
    log_adjustment();
#ifdef DEBUG
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot adjust remote logs\n");
    }    
#endif    

//sprintf(posted_sends_str, "servers:");
//HRT_GET_TIMESTAMP(SRV_DATA->t1);
    /* Phase 2: Direct log update */
#ifdef DEBUG    
    rc = 
#endif    
    update_remote_logs();
#ifdef DEBUG    
    if (RC_ERROR == rc) {
        /* This should never happen */
        error_return(1, log_fp, "Cannot update remote logs\n");
    }
#endif    
//HRT_GET_TIMESTAMP(SRV_DATA->t2);
//HRT_GET_ELAPSED_TICKS(SRV_DATA->t1, SRV_DATA->t2, &ticks);
//info(log_fp, "Log update (%s): %lf\n", posted_sends_str, HRT_GET_USEC(ticks)); 
    if (wait_for_commit && committed) {
        //HRT_GET_TIMESTAMP(SRV_DATA->t2);
#if 0
        wrl_idx++;
        if (wrl_idx == 1000) {
            qsort(wrl_count_array, 1000, sizeof(uint64_t), cmpfunc_uint64);
            info(log_fp, "WRL COUNT: %"PRIu64" (%"PRIu64", %"PRIu64")\n", 
                    wrl_count_array[500], wrl_count_array[19], 
                    wrl_count_array[1000-21]);
            wrl_idx = 0;
        }
#endif        
        return 0;
    }
    if (threshold == 1000) {
        //info_wtime(log_fp, "Threshold reached\n");
        return 0;
    }
    if (wait_for_commit) goto loop_for_commit;
    
    return 0;
}

/**
 * Function used for sorting an array of offsets
 */
static int 
cmpfunc_offset( const void *a, const void *b )
{
    //return ( *(uint64_t*)a - *(uint64_t*)b );
    int result = log_is_offset_larger(SRV_DATA->log, *(uint64_t*)a, *(uint64_t*)b);
    if (result) {
        return 1;
    }
    return -1;
}

/**
 * Get remote apply offsets
 * Note: do not wait for the reads to complete;
 * yet, we must avoid having more than IBDEV->ib_dev_attr.max_qp_rd_atom
 * outstanding read operations 
 */
int rc_get_remote_apply_offsets()
{
    int rc, init;
    server_t *server;
    dare_ib_ep_t *ep;
    rem_mem_t rm;
    uint32_t offset;
    uint8_t i, size;
    TIMER_INIT;
    
    size = get_extended_group_size(SRV_DATA->config);
    for (init = 0, i = 0; i < size; i++) {
        if ( (i == SRV_DATA->config.idx) || 
            !CID_IS_SERVER_ON(SRV_DATA->config.cid, i) )
        {
            SRV_DATA->ctrl_data->apply_offsets[i] = SRV_DATA->log->apply;
            continue;
        }
        
        /* Server is on and it's not me */
        server = &SRV_DATA->config.servers[i];
        if (server->fail_count >= PERMANENT_FAILURE) {
            /* The leader suspects this server */
            continue;
        }
        
        ep = (dare_ib_ep_t*)server->ep;
        if (0 == ep->rc_connected) {
            continue;
        }
        if (SRV_DATA->log->len == SRV_DATA->ctrl_data->vote_ack[i]) {
            /* No vote ACK from this server */
            continue;
        }
        if (server->last_get_read_ssn) {
            /* Still waiting for another read */
            continue;
        }
        if (!init) {
            ssn++;  // increase ssn to avoid past work completions
            TIMER_START(log_fp, "Get remote apply offsets (%"PRIu64")\n", ssn); 
            //info_wtime(log_fp, "Get remote apply offsets (%"PRIu64")\n", ssn); 
            offset = (uint32_t) (offsetof(dare_log_t, apply));
            init = 1;
        }
        server->last_get_read_ssn = ssn;

        text(log_fp, "   (p%"PRIu8")\n", i);
        //info(log_fp, "   # (p%"PRIu8") r=%"PRIu64"\n", i, 
        //    SRV_DATA->ctrl_data->apply_offsets[i]);
    
        rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
        rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;
        
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(i, LOG_QP, &SRV_DATA->ctrl_data->apply_offsets[i],
                        sizeof(uint64_t), IBDEV->lcl_mr[CTRL_QP],
                        IBV_WR_RDMA_READ, NOTSIGNALED, rm, NULL);
        if (0 != rc) {
            /* This should never happen */
            error_return(RC_ERROR, log_fp, "Cannot post send operation\n");
        }        
    }
    if (init) {
        TIMER_STOP(log_fp);
    }
    
    return 0;
}

#endif

/* ================================================================== */
/* Handle QPs state */
#if 1

/** 
 * Restarts a certain QP: *->RESET->INIT->RTR->RTS
 * used only in case of ERROR
 */
static int 
rc_qp_restart( dare_ib_ep_t *ep, int qp_id )
{    
    int rc;

    TIMER_INIT;

    TIMER_START(log_fp, "# Restarting QP...");
    //ev_tstamp start_ts = ev_now(SRV_DATA->loop);
    
    rc = rc_qp_reset(ep, qp_id);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot move QP to reset state\n");
    }
    rc = rc_qp_reset_to_init(ep, qp_id);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot move QP to init state\n");
    }
    rc = rc_qp_init_to_rtr(ep, qp_id);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot move QP to RTR state\n");
    }
    rc = rc_qp_rtr_to_rts(ep, qp_id);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot move QP to RTS state\n");
    }
    TIMER_STOP(log_fp);
    //info_wtime(log_fp, " # Restart QP: %lf (ms)\n", (ev_now(SRV_DATA->loop) - start_ts)*1000);
//debug(log_fp, "QP NUM=%"PRIu32"\n", ep->rc_ep.rc_qp[qp_id].qp->qp_num);
    
    return 0;
}

/** 
 * Disconnect both QPs for a given server: *->RESET
 * used for server removals
 */
int rc_disconnect_server( uint8_t idx )
{
    int rc;
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[idx].ep;
    if (0 == ep->rc_connected) {
        /* No RC data for this endpoint */
        return 0;
    }
    //ev_tstamp start_ts = ev_now(SRV_DATA->loop);
    
    ep->ud_ep.lid = 0;
    ep->rc_connected = 0;
    ud_ah_destroy(ep->ud_ep.ah);
    ep->ud_ep.ah = NULL;
    rc = rc_qp_reset(ep, LOG_QP);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot reset LOG QP for p%"PRIu8"\n", idx);
    }
    rc = rc_qp_reset(ep, CTRL_QP);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot reset CTRL QP for p%"PRIu8"\n", idx);
    }
text(log_fp, "DISCONNECTED p%"PRIu8"\n", idx);
    //info_wtime(log_fp, " # Disconnect server: %lf (ms)\n", (ev_now(SRV_DATA->loop) - start_ts)*1000);
    return 0;
}

/** 
 * Connect a certain QP with a server: RESET->INIT->RTR->RTS
 * used for server arrivals
 */
int rc_connect_server( uint8_t idx, int qp_id )
{
    int rc;
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[idx].ep;
    
    //ev_tstamp start_ts = ev_now(SRV_DATA->loop);
    
    ibv_query_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, IBV_QP_STATE, &init_attr);
    if (attr.qp_state != IBV_QPS_RESET) {
        rc = rc_qp_reset(ep, qp_id);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot move QP to reset state\n");
        }
    }
    
    rc = rc_qp_reset_to_init(ep, qp_id);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot move QP to init state\n");
    }
    rc = rc_qp_init_to_rtr(ep, qp_id);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot move QP to RTR state\n");
    }
    rc = rc_qp_rtr_to_rts(ep, qp_id);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot move QP to RTS state\n");
    }

    //info_wtime(log_fp, " # Connect server: %lf (ms)\n", (ev_now(SRV_DATA->loop) - start_ts)*1000);
    return 0;
}

/**
 * Revoke remote log access; that is, 
 * move all log QPs to RESET state: *->RESET
 */
int rc_revoke_log_access()
{
    int rc;
    uint8_t i, size = get_extended_group_size(SRV_DATA->config);
    dare_ib_ep_t *ep;
    
    for (i = 0; i < size; i++) {
        if (i == SRV_DATA->config.idx) continue;
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
            continue;
        }
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) {
            /* No RC data */
            continue;
        }
        if (0 == ep->log_access) {
            /* No LOG access */
            continue;
        }
        
        rc = rc_qp_reset(ep, LOG_QP);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot revoke the access of LID=%"PRIu16"\n",
                         ep->ud_ep.lid);
        }
        ep->log_access = 0;
    }
    info_wtime(log_fp, "[T%"PRIu64"] Revoked log access\n", 
                        SID_GET_TERM(SRV_DATA->ctrl_data->sid));
    //debug(log_fp, "Log access revoked\n");

    return 0;
}

/**
 * Grant remote log access to the leader; that is, 
 * move log QP to RTS state: RESET->INIT->RTR->RTS
 */
int rc_restore_log_access()
{
    int rc;
    uint8_t i, leader_idx, 
            size = get_extended_group_size(SRV_DATA->config);
    dare_ib_ep_t *ep;

    leader_idx = SID_GET_IDX(SRV_DATA->ctrl_data->sid); 
    if (leader_idx != SRV_DATA->config.idx) {
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, leader_idx)) {
            error_return(1, log_fp, "Leader is OFF\n");
        }
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[leader_idx].ep;
        if (0 == ep->rc_connected) {
            /* No RC data */
            error_return(1, log_fp, "Leader has not RC data\n");
        }
        if (1 == ep->log_access) {
            /* Already LOG access */
            info_wtime(log_fp, "[T%"PRIu64"] Already granted log access to p%"PRIu8"\n", 
                                    SID_GET_TERM(SRV_DATA->ctrl_data->sid), 
                                    SID_GET_IDX(SRV_DATA->ctrl_data->sid));
            return 0;
        }
        rc = rc_qp_reset_to_rts(ep, LOG_QP);
        if (rc) {
            error_return(1, log_fp, "rc_qp_reset_to_rts\n");
        }
        ep->log_access = 1;
        info_wtime(log_fp, "[T%"PRIu64"] Granted log access to p%"PRIu8"\n", 
                                    SID_GET_TERM(SRV_DATA->ctrl_data->sid), 
                                    SID_GET_IDX(SRV_DATA->ctrl_data->sid));
    }
    else if (SID_GET_L(SRV_DATA->ctrl_data->sid)) {
        for (i = 0; i < size; i++) {
            if (i == SRV_DATA->config.idx) continue;
            if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
                continue;
            }
            ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
            if (0 == ep->rc_connected) {
                /* No RC data */
                continue;
            }
            if (1 == ep->log_access) {
                /* Already LOG access */
                continue;
            }
            rc = rc_qp_reset_to_rts(ep, LOG_QP);
            if (rc) {
                error_return(1, log_fp, "rc_qp_reset_to_rts\n");
            }
            ep->log_access = 1;
        }
        info_wtime(log_fp, "[T%"PRIu64"] Gained log access\n", 
                                    SID_GET_TERM(SRV_DATA->ctrl_data->sid));
    }
    //debug(log_fp, "Log access restored\n");

    return 0;
}

/* ================================================================== */

/**
 * Move a QP to the RESET state 
 */
static int
rc_qp_reset( dare_ib_ep_t *ep, int qp_id )
{
    int rc;
    //struct ibv_wc wc;
    struct ibv_qp_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RESET;
    //while (ibv_poll_cq(IBDEV->rc_cq[qp_id], 1, &wc) > 0);
    //empty_completion_queue(0, qp_id, 0, NULL);
    rc = ibv_modify_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, IBV_QP_STATE); 
    if (0 != rc) {
        error_return(1, log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
    }
    
    return 0;
}

/**
 * Transit a QP from RESET to INIT state 
 */
static int 
rc_qp_reset_to_init( dare_ib_ep_t *ep, int qp_id )
{
    int rc;
    struct ibv_qp_attr attr;
//    struct ibv_qp_init_attr init_attr;
//    ibv_query_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, IBV_QP_STATE, &init_attr);
//    info(log_fp, "[QP Info (LID=%"PRIu16")] %s QP: %s\n",
//         ep->ud_ep.lid, (LOG_QP == qp_id) ? "log" : "ctrl",
//         qp_state_to_str(attr.qp_state));
    
    /* Reset state */
    ep->rc_ep.rc_qp[qp_id].signaled_wr_id = 0;
    ep->rc_ep.rc_qp[qp_id].send_count = 0;
    ep->rc_ep.rc_qp[qp_id].state = RC_QP_ACTIVE;

    memset(&attr, 0, sizeof(attr));
    attr.qp_state        = IBV_QPS_INIT;
    attr.pkey_index      = 0;
    attr.port_num        = IBDEV->port_num;
    attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE | 
                           IBV_ACCESS_REMOTE_READ |
                           IBV_ACCESS_REMOTE_ATOMIC |
                           IBV_ACCESS_LOCAL_WRITE;

    rc = ibv_modify_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, 
                        IBV_QP_STATE | IBV_QP_PKEY_INDEX | 
                        IBV_QP_PORT | IBV_QP_ACCESS_FLAGS); 
    if (0 != rc) {
        error_return(1, log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
    }
    return 0;
}

/**
 * Transit a QP from INIT to RTR state 
 */
static int 
rc_qp_init_to_rtr( dare_ib_ep_t *ep, int qp_id )
{
    int rc;
    struct ibv_qp_attr attr;
#ifdef TERM_PSN    
    uint32_t psn;
    if (LOG_QP == qp_id) {
        uint64_t term = SID_GET_TERM(SRV_DATA->ctrl_data->sid);
        psn = (uint32_t)(term & 0xFFFFFF);
    }
#endif    
    //struct ibv_qp_init_attr init_attr;
    //uint8_t max_dest_rd_atomic;
    //ibv_query_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, IBV_QP_MAX_DEST_RD_ATOMIC, &init_attr);
    //max_dest_rd_atomic = attr.max_dest_rd_atomic;

    /* Move the QP into the RTR state */
    memset(&attr, 0, sizeof(attr));
    attr.qp_state           = IBV_QPS_RTR;
    /* Setup attributes */
    attr.path_mtu           = ep->mtu;
    attr.max_dest_rd_atomic = IBDEV->ib_dev_attr.max_qp_rd_atom;
    attr.min_rnr_timer      = 12;
    attr.dest_qp_num        = ep->rc_ep.rc_qp[qp_id].qpn;
#ifdef TERM_PSN    
    attr.rq_psn             = (LOG_QP == qp_id) ? psn : CTRL_PSN;
#else    
    attr.rq_psn             = (LOG_QP == qp_id) ? LOG_PSN : CTRL_PSN;
#endif    
    /* Note: this needs to modified for the lock; see rc_log_qp_lock */
    attr.ah_attr.is_global     = 0;
    attr.ah_attr.dlid          = ep->ud_ep.lid;
    attr.ah_attr.port_num      = IBDEV->port_num;
    attr.ah_attr.sl            = 0;
    attr.ah_attr.src_path_bits = 0;

    rc = ibv_modify_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, 
                        IBV_QP_STATE | IBV_QP_PATH_MTU |
                        IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER | 
                        IBV_QP_RQ_PSN | IBV_QP_AV | IBV_QP_DEST_QPN);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
    }
    
    //debug(log_fp, "Move %s QP of lid=%"PRIu16" into RTR state RQ_PSN=%"PRIu32"\n", 
    //               log ? "log":"ctrl", ep->ud_ep.lid, attr.rq_psn);
    return 0;
}

/**
 * Transit a QP from RTR to RTS state
 */
static int 
rc_qp_rtr_to_rts( dare_ib_ep_t *ep, int qp_id )
{
    int rc;
    struct ibv_qp_attr attr;
#ifdef TERM_PSN    
    uint32_t psn;
    if (LOG_QP == qp_id) {
        uint64_t term = SID_GET_TERM(SRV_DATA->ctrl_data->sid);
        psn = (uint32_t)(term & 0xFFFFFF);
    }
#endif    
    //struct ibv_qp_init_attr init_attr;
    //ibv_query_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, IBV_QP_MAX_QP_RD_ATOMIC | IBV_QP_MAX_DEST_RD_ATOMIC, &init_attr);
    //info_wtime(log_fp, "RC QP[%s] max_rd_atomic=%"PRIu8"; max_dest_rd_atomic=%"PRIu8"\n", qp_id == LOG_QP ? "LOG" : "CTRL", attr.max_rd_atomic, attr.max_dest_rd_atomic);

    /* Move the QP into the RTS state */
    memset(&attr, 0, sizeof(attr));
    attr.qp_state       = IBV_QPS_RTS;
    //attr.timeout        = 5;    // ~ 131 us
    attr.timeout        = 1;    // ~ 8 us
    attr.retry_cnt      = 0;    // max is 7
    attr.rnr_retry      = 7;
#ifdef TERM_PSN    
    attr.sq_psn         = (LOG_QP == qp_id) ? psn : CTRL_PSN;
#else    
    attr.sq_psn         = (LOG_QP == qp_id) ? LOG_PSN : CTRL_PSN;
#endif    
//debug(log_fp, "MY SQ PSN: %"PRIu32"\n", attr.sq_psn);
    attr.max_rd_atomic = IBDEV->ib_dev_attr.max_qp_rd_atom;

    rc = ibv_modify_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, 
                        IBV_QP_STATE | IBV_QP_TIMEOUT |
                        IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | 
                        IBV_QP_SQ_PSN | IBV_QP_MAX_QP_RD_ATOMIC);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
    }
    
    //ibv_query_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, IBV_QP_MAX_QP_RD_ATOMIC | IBV_QP_MAX_DEST_RD_ATOMIC, &init_attr);
    //info_wtime(log_fp, "RC QP[%s] max_rd_atomic=%"PRIu8"; max_dest_rd_atomic=%"PRIu8"\n", qp_id == LOG_QP ? "LOG" : "CTRL", attr.max_rd_atomic, attr.max_dest_rd_atomic);

    
    //debug(log_fp, "Move %s QP of lid=%"PRIu16" into RTS state SQ_PSN=%"PRIu32"\n", 
    //               qp_id==LOG_QP ? "log":"ctrl", ep->ud_ep.lid, attr.sq_psn);
    return 0;
}

static int 
rc_qp_reset_to_rts(dare_ib_ep_t *ep, int qp_id)
{
    if (rc_qp_reset(ep, qp_id)) {
        error_return(1, log_fp, "Cannot move LOG QP to reset state\n");
    }
    if (rc_qp_reset_to_init(ep, qp_id)) {
        error_return(1, log_fp, "Cannot move QP to init state\n");
    }
    if (rc_qp_init_to_rtr(ep, qp_id)) {
        error_return(1, log_fp, "Cannot move QP to RTR state\n");
    }
    if (rc_qp_rtr_to_rts(ep, qp_id)) {
        error_return(1, log_fp, "Cannot move QP to RTS state\n");
    }
}


int rc_print_qp_state( void *data )
{
    int rc;
    dare_ib_ep_t *ep = (dare_ib_ep_t*)data;

    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
     
    rc = ibv_query_qp(ep->rc_ep.rc_qp[LOG_QP].qp, &attr, IBV_QP_STATE, &init_attr);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_query_qp failed because %s\n",
                    strerror(rc));
    }
    info(log_fp, "[QP Info] LID=%"PRIu16" -> log_qp: %s\n", 
         ep->ud_ep.lid, qp_state_to_str(attr.qp_state));

    rc = ibv_query_qp(ep->rc_ep.rc_qp[CTRL_QP].qp, &attr, IBV_QP_STATE, &init_attr);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_query_qp failed because %s\n",
                    strerror(rc));
    }
    info(log_fp, "[QP Info] LID=%"PRIu16" -> ctrl_qp: %s\n", 
         ep->ud_ep.lid, qp_state_to_str(attr.qp_state));

    return 0;
}

#endif

/* ================================================================== */
/* Handle RDMA operations */
#if 1

/**
 * Post send operation
 */
int loggp_not_inline;
static int 
post_send( uint8_t server_id, 
           int qp_id,
           void *buf,
           uint32_t len,
           struct ibv_mr *mr,
           enum ibv_wr_opcode opcode,
           int signaled,
           rem_mem_t rm,
           int *posted_sends )
{
    int rc, wait_signaled_wr = 0;
    uint32_t *send_count_ptr;
    uint64_t *signaled_wrid_ptr;
    uint8_t  *qp_state_ptr;
    dare_ib_ep_t *ep;
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;

    /* Define some temporary variables */
    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[server_id].ep;
    send_count_ptr = &(ep->rc_ep.rc_qp[qp_id].send_count);
    signaled_wrid_ptr = &(ep->rc_ep.rc_qp[qp_id].signaled_wr_id);
    qp_state_ptr = &(ep->rc_ep.rc_qp[qp_id].state);
    
    if (RC_QP_BLOCKED == *qp_state_ptr) {
        /* This QP is blocked; need to wait for the signaled WR */
        info_wtime(log_fp, "%s QP of p%"PRIu8" is BLOCKED\n", qp_id == LOG_QP ? "LOG": "CTRL", server_id);
        rc = empty_completion_queue(server_id, qp_id, 1, NULL);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot empty completion queue\n");
        }
        //info_wtime(log_fp, "QP can be restarted now\n");
    }
    if (RC_QP_ERROR == *qp_state_ptr) {
        /* This QP is in ERR state - restart it */
        rc_qp_restart(ep, qp_id);
        *qp_state_ptr = RC_QP_ACTIVE;
        *send_count_ptr = 0;
    }
    
    /* Increment number of posted sends to avoid QP overflow */
    (*send_count_ptr)++;
    //info_wtime(log_fp, "(ssn=%"PRIu64":p%"PRIu8") send_count[%s] = %"PRIu32"\n", ssn, server_id, qp_id == LOG_QP ? "LOG" : "CTRL", *send_count_ptr);
 
    /* Local memory */
    memset(&sg, 0, sizeof(sg));
    sg.addr   = (uint64_t)buf;
    sg.length = len;
    sg.lkey   = mr->lkey;
 
    memset(&wr, 0, sizeof(wr));
    WRID_SET_SSN(wr.wr_id, ssn);
    WRID_SET_CONN(wr.wr_id, server_id);
    if (wa_flag) {
        WRID_SET_WA(wr.wr_id);
        wa_flag = 0;
    }
    wr.sg_list    = &sg;
    wr.num_sge    = 1;
    wr.opcode     = opcode;
    if ( (*signaled_wrid_ptr != 0) && 
        (WRID_GET_TAG(*signaled_wrid_ptr) == 0) ) 
    {
        /* Signaled WR was found */
        *signaled_wrid_ptr = 0;
        //info_wtime(log_fp, "(signaled WR found) send_count[%s] = %"PRIu32"\n", qp_id == LOG_QP ? "LOG" : "CTRL", *send_count_ptr);
    }
    if ( (*send_count_ptr == IBDEV->rc_max_send_wr >> 2) && (*signaled_wrid_ptr == 0) ) {
        /* A quarter of the Send Queue is full; add a special signaled WR */
        wr.send_flags |= IBV_SEND_SIGNALED;
        WRID_SET_TAG(wr.wr_id);    // special mark
        *signaled_wrid_ptr = wr.wr_id;
        //info_wtime(log_fp, "Signaled WR added on QP %s for p%"PRIu8" with ssn=%"PRIu64"\n", qp_id == LOG_QP ? "LOG" : "CTRL", server_id, WRID_GET_SSN(*signaled_wrid_ptr));
        //info_wtime(log_fp, "SSN = %"PRIu64"\n", ssn);
        *send_count_ptr = 0;
    }
    else if (*send_count_ptr == 3 * IBDEV->rc_max_send_wr >> 2) {
        if (*signaled_wrid_ptr != 0) {
            /* The Send Queue is full; need to wait for the signaled WR */
            wait_signaled_wr = 1;
            //info_wtime(log_fp, "(waiting for signaled WR) send_count[%s] = %"PRIu32"\n", qp_id == LOG_QP ? "LOG" : "CTRL", *send_count_ptr);
            //info_wtime(log_fp, "waiting for:"); PRINT_WRID_(*signaled_wrid_ptr);
        }
    }
    if (signaled) {
        wr.send_flags |= IBV_SEND_SIGNALED;
    }
    if (IBV_WR_RDMA_WRITE == opcode) {
        if ( (len <= IBDEV->rc_max_inline_data) && !loggp_not_inline ) {
            wr.send_flags |= IBV_SEND_INLINE;
        }
    }   
    wr.wr.rdma.remote_addr = rm.raddr;
    wr.wr.rdma.rkey        = rm.rkey;
    rc = ibv_post_send(ep->rc_ep.rc_qp[qp_id].qp, &wr, &bad_wr);
    if (0 != rc) {
        //info(log_fp, "POST ERROR: ssn=%"PRIu64":%"PRIu8"; next=%p; num_sge=%d, opcode=%s\n", 
            //WRID_GET_SSN(bad_wr->wr_id), WRID_GET_CONN(bad_wr->wr_id), 
            //bad_wr->next, bad_wr->num_sge,
            //(bad_wr->opcode == IBV_WR_RDMA_READ ? "RDMA_READ" : 
            //(bad_wr->opcode == IBV_WR_RDMA_WRITE ? "RDMA_WRITE" : "OTHER")));
        error_return(1, log_fp, "ibv_post_send failed because %s [%s]\n", 
            strerror(rc), rc == EINVAL ? "EINVAL" : rc == ENOMEM ? "ENOMEM" : rc == EFAULT ? "EFAULT" : "UNKNOWN");
    }
    
    rc = empty_completion_queue(server_id, qp_id, wait_signaled_wr, posted_sends);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot empty completion queue\n");
    }
    
    return 0;
}

static int
empty_completion_queue( uint8_t server_id,
                        int qp_id,
                        int wait_signaled_wr,
                        int *posted_sends )
{
    int rc, ne, i;
    int can_restart_qp;
    uint64_t wr_id;
    uint64_t *signaled_wrid_ptr;
    uint8_t  *qp_state_ptr;
    uint8_t k, size = get_extended_group_size(SRV_DATA->config);
    uint8_t conn;
    server_t *server;
    dare_ib_ep_t *ep;
    //info_wtime(log_fp, "calling empty_completion_queue %d\n", wait_signaled_wr);
    
    while(1) {
        /* Read as many WCs as possible ... */
        ne = ibv_poll_cq(IBDEV->rc_cq[qp_id], IBDEV->rc_cqe, 
                        IBDEV->rc_wc_array);
        if (0 == ne) {
            /* ... but do not wait for them... */
            if (wait_signaled_wr) {
                /* ... unless the send queue is full; need to wait for the signaled WR */
                continue;
            }
            break;
        }
#ifdef DEBUG        
        if (ne < 0) {
            error_return(1, log_fp, "ibv_poll_cq() failed\n");
        }
#endif        
        //info_wtime(log_fp, "ne=%d\n", ne);
        for (i = 0; i < ne; i++) {
#if 0
            /* Define some temporary variables */
            wr_id = IBDEV->rc_wc_array[i].wr_id;
            
            //info_wtime(log_fp, "WR completed:"); PRINT_WRID_(wr_id);
            
            /* Go through all QPs and check if this WR is a special signaled WR */
            can_restart_qp = 1;
            for (k = 0; k < size; k++) {
                /* Define some more temporary variables */
                ep = (dare_ib_ep_t*)SRV_DATA->config.servers[k].ep;
                signaled_wrid_ptr = &(ep->rc_ep.rc_qp[qp_id].signaled_wr_id);
                qp_state_ptr = &(ep->rc_ep.rc_qp[qp_id].state);
                
                if (0 == *signaled_wrid_ptr) continue;    
                if (wr_id == *signaled_wrid_ptr) {
                    /* Special signaled WR found - untag it */
                    WRID_UNSET_TAG(*signaled_wrid_ptr);
                    if (RC_QP_BLOCKED == *qp_state_ptr) {
                        /* Unblock QP */
                        *qp_state_ptr = RC_QP_ERROR;
                    }
                    if (k == server_id) {
                        /* This is the signaled WR we were waiting for */
                        wait_signaled_wr = 0;
                    }
                    break;
                }
                else if (WRID_GET_CONN(wr_id) == WRID_GET_CONN(*signaled_wrid_ptr)) {
                    /* This WR is from the same QP as a signaled WR; 
                    in case of error, we cannot restart the QP to not lose the signaled WR */
                    can_restart_qp = 0;
                }
            }
#else 
            /* Define some temporary variables */
            wr_id = IBDEV->rc_wc_array[i].wr_id;
            conn = WRID_GET_CONN(wr_id);
            server = &SRV_DATA->config.servers[conn];
            ep = (dare_ib_ep_t*)server->ep;
    
            /* Check last get read ssn */
            if ( (LOG_QP == qp_id) && 
                (WRID_GET_SSN(wr_id) >= server->last_get_read_ssn) ) 
            {
                server->last_get_read_ssn = 0;
            }

            /* Check signaled WR */
            can_restart_qp = 1;
            signaled_wrid_ptr = &(ep->rc_ep.rc_qp[qp_id].signaled_wr_id);
            qp_state_ptr = &(ep->rc_ep.rc_qp[qp_id].state);
            if (0 != *signaled_wrid_ptr) {
                /* There is a signaled WR on this queue */
                if (wr_id == *signaled_wrid_ptr) {
                    /* Special signaled WR found - untag it */
                    WRID_UNSET_TAG(*signaled_wrid_ptr);
                    //info_wtime(log_fp, "Found signal for QP %d of p%"PRIu8"\n", qp_id, conn);
                    if (RC_QP_BLOCKED == *qp_state_ptr) {
                        /* Unblock QP */
                        *qp_state_ptr = RC_QP_ERROR;
                        //info_wtime(log_fp, "Unblock QP %d of p%"PRIu8"\n", qp_id, conn);
                    }
                    if (conn == server_id) {
                        /* This is the signaled WR we were waiting for */
                        wait_signaled_wr = 0;
                    }
                }
                else {
                    /* This WR is from the same QP as a signaled WR; 
                    in case of error, we cannot restart the QP to not lose the signaled WR */
                    //info_wtime(log_fp, "Cannot reset QP %d of p%"PRIu8"; waiting for ssn=%"PRIu64"\n", qp_id, conn, WRID_GET_SSN(*signaled_wrid_ptr));
                    //info_wtime(log_fp, "Current ssn=%"PRIu64"\n", WRID_GET_SSN(wr_id));
                    can_restart_qp = 0;
                }
            }
#endif            

            rc = handle_work_completion(&(IBDEV->rc_wc_array[i]), qp_id);
            handle_lr_work_completion(i, rc);
            if (WC_SUCCESS == rc) {
                /* Successful send operation: reset failure count */
                server->fail_count = 0;
                if ( (WRID_GET_SSN(wr_id) == ssn) && (posted_sends) ) {
                    /* Mark send as successful */
                    posted_sends[conn]--;
                }
                TIMER_INFO(log_fp, "      [+(p%"PRIu8",%"PRIu64")]\n", 
                            conn, WRID_GET_SSN(wr_id));            
            }
            else if (WC_FAILURE == rc) {
                TIMER_INFO(log_fp, "      [-(p%"PRIu8",%"PRIu64")]\n", 
                        conn, WRID_GET_SSN(wr_id));  
                if (can_restart_qp) {
                    /* Move QP into ERR state */
                    ep->rc_ep.rc_qp[qp_id].state = RC_QP_ERROR;
                }
                else {
                    /* To not lose the signaled WR, we restart the QP later;
                    however, we cannot post new WRs in this QP since it's in the ERR state*/
                    /* This if must be here !!! */
                    if (RC_QP_ERROR != ep->rc_ep.rc_qp[qp_id].state)  {
                   // info_wtime(log_fp, "[%s:%d] Blocking QP %d of p%"PRIu8"\n", 
                   //         __func__, __LINE__, qp_id, conn);
                        ep->rc_ep.rc_qp[qp_id].state = RC_QP_BLOCKED;
                    }
/* Note: In order to reuse a QP, it can be transitioned to Reset 
state from any state by calling to ibv_modify_qp(). If prior to 
this state transition, there were any Work Requests or completions 
in the send or receive queues of that QP, they will be cleared 
from the queues. */                    
                }
                /* Operation fail: increase failure count */
                if (CTRL_QP == qp_id) {
                    server->fail_count++;
                }
                if ( (WRID_GET_SSN(wr_id) == ssn) && (posted_sends) ) {
                    /* Mark send as unsuccessful */
                    posted_sends[conn] = -1;
                }
            }
            else if (WC_SOFTWARE_BUG == rc) {
                /* WR failed due to a software bug */
                info_wtime(log_fp, "Software bug!!!!!\n");
            }
        }
    }
    return 0;
}

/**
 * Wait for a majority of success array entries to be set
 * @param posted_sends array with number of posted sends per server
 */
static int
wait_for_majority( int *posted_sends, int qp_id )
{
    int rc, ne;
    int can_restart_qp;
    server_t *server;
    uint64_t wr_id;
    uint64_t *signaled_wrid_ptr;
    uint8_t  *qp_state_ptr;
    dare_ib_ep_t *ep;
    uint8_t i, j, k, size = get_group_size(SRV_DATA->config);
    uint8_t success_count, posted_send_count;
    uint8_t conn;
    
    //TIMER_INIT;
    
    //PRINT_CID_(SRV_DATA->config.cid);
    //TIMER_START(log_fp, "Waiting for a majority of send ops to complete...");
    
    /* Compute number of posted sends */
//    info(log_fp, "posted sends: ");
    for (i = 0, posted_send_count = 0; i < size; i++) {
//        info(log_fp, "%d ", posted_sends[i]);
        if (posted_sends[i] < 0)
            continue;
        posted_send_count += (uint8_t)posted_sends[i];
    }
//    info(log_fp, "\n");
    
    /* Wait for send operations to complete */
    j = 0;
    while(j < 2) {
        size = SRV_DATA->config.cid.size[j];
        for (i = 0, success_count = 0; i < size; i++) {
            if (posted_sends[i] < 0) {
                continue;
            }
            if (posted_sends[i] == 0) {
                success_count++;
            }
        }
//info(log_fp, "WAIT FOR MAJORITY: posted_send_count=%"PRIu8"; success_count=%"PRIu8"; size=%"PRIu8"\n", posted_send_count, success_count, size);
        while ( (success_count <= size / 2) && (posted_send_count) ) {
            ne = ibv_poll_cq(IBDEV->rc_cq[qp_id], IBDEV->rc_cqe, 
                        IBDEV->rc_wc_array);
            if (ne < 0) {
                /* Failure */
                error_return(RC_ERROR, log_fp, "Couldn't poll completion queue\n");
            }
            /* Check all completed WCs */
            for (i = 0; i < (uint8_t)ne; i++) {
                /* Define some temporary variables */
                wr_id = IBDEV->rc_wc_array[i].wr_id;
                conn = WRID_GET_CONN(wr_id);
                server = &SRV_DATA->config.servers[conn];                
                ep = (dare_ib_ep_t*)server->ep;
                //info_wtime(log_fp, "WR completed:"); PRINT_WRID_(wr_id);

                /* Check last get read ssn */
                if ( (LOG_QP == qp_id) && 
                    (WRID_GET_SSN(wr_id) >= server->last_get_read_ssn) )
                {
                    server->last_get_read_ssn = 0;
                }

                /* Check signaled WR */
                can_restart_qp = 1;
                signaled_wrid_ptr = &(ep->rc_ep.rc_qp[qp_id].signaled_wr_id);
                qp_state_ptr = &(ep->rc_ep.rc_qp[qp_id].state);
                if (0 != *signaled_wrid_ptr) {
                    /* There is a signaled WR on this queue */
                    if (wr_id == *signaled_wrid_ptr) {
                        /* Special signaled WR found - untag it */
                        WRID_UNSET_TAG(*signaled_wrid_ptr);
                        if (RC_QP_BLOCKED == *qp_state_ptr) {
                            /* Unblock QP */
                            *qp_state_ptr = RC_QP_ERROR;
                        }
                    }
                    else {
                        /* This WR is from the same QP as a signaled WR; 
                        in case of error, we cannot restart the QP to not lose the signaled WR */
                        can_restart_qp = 0;
                    }
                }

#if 0                
                /* Check if this WR is a special signaled WR */
                can_restart_qp = 1;
                for (k = 0; k < size; k++) {
                    /* Define some more temporary variables */
                    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[k].ep;
                    signaled_wrid_ptr = &(ep->rc_ep.rc_qp[qp_id].signaled_wr_id);
                    qp_state_ptr = &(ep->rc_ep.rc_qp[qp_id].state);
                    
                    if (0 == *signaled_wrid_ptr) continue;
                    if (wr_id == *signaled_wrid_ptr) {
                        /* Special signaled WR found - untag */
                        WRID_UNSET_TAG(*signaled_wrid_ptr);
                        if (RC_QP_BLOCKED == *qp_state_ptr) {
                            /* Unblock QP */
                            *qp_state_ptr = RC_QP_ERROR;
                        }
                        break;
                    }
                    else if (WRID_GET_CONN(wr_id) == WRID_GET_CONN(*signaled_wrid_ptr)) {
                        /* This WR is from the same QP as a signaled WR; 
                        in case of error, we cannot restart the QP to not lose the signaled WR */
                        can_restart_qp = 0;
                    }
                }
#endif                
                if (WRID_GET_SSN(wr_id) == ssn) {
                    /* Decrement the number of sends posted in this round */
                    posted_send_count--;
                }
                if (0 == ep->rc_connected) {
                    /* This should never happen */
                    continue;
                }
                
                /* Handle the WC */
                rc = handle_work_completion(&(IBDEV->rc_wc_array[0]), qp_id);
                handle_lr_work_completion(i, rc);
                if (WC_SUCCESS == rc) {
                    /* Successful send operation: reset failure count */
                    server->fail_count = 0;
                    TIMER_INFO(log_fp, "      [+(p%"PRIu8",%"PRIu64")]\n",
                            conn, WRID_GET_SSN(wr_id));            
                    if (ssn > WRID_GET_SSN(wr_id)) {
                        /* Work completion from a previous posted sends */
                        continue;
                    }                     
                    /* Decrement the number of WC expected for this ep */
                    posted_sends[conn]--;
                    if (posted_sends[conn]) {
                        /* Need to wait for all sends to complete */
                        continue;
                    }
                    if (conn < size) {
                        success_count++;
                    }
                    continue;
                }
                if (WC_FAILURE == rc) {
                    /* The local QP moves into ERR state - restart it if possible 
                    if not, the QP needs to be blocked since it's in the ERR state */
                    if (can_restart_qp) {
                        /* Move QP into ERR state */
                        ep->rc_ep.rc_qp[qp_id].state = RC_QP_ERROR;
                    }
                    else {
                        /* To not lose the signaled WR, we restart the QP later;
                        however, we cannot post new WRs in this QP since it's in the ERR state*/
                        if (RC_QP_ERROR != ep->rc_ep.rc_qp[qp_id].state) {
                            //info_wtime(log_fp, "[%s:%d] Blocking QP %d of p%"PRIu8"\n", __func__, __LINE__, qp_id, conn);
                            ep->rc_ep.rc_qp[qp_id].state = RC_QP_BLOCKED;
                        }
                    }

                    /* Operation fail: increase failure count */
                    if (CTRL_QP == qp_id) {
                        server->fail_count++;
                    }
                    TIMER_INFO(log_fp, "       [-(p%"PRIu8",%"PRIu64")]\n",
                            conn, WRID_GET_SSN(wr_id));
                    continue;
                }
                if (WC_SOFTWARE_BUG == rc) {
                    /* WR failed due to a software bug */
                    debug(log_fp, "Software bug!!!!!\n");
                    continue;
                }
                if (WC_ERROR == rc) {
                    // TODO: find out the error for QP inaccessible
                }
            }
        }
        if (CID_STABLE == SRV_DATA->config.cid.state) {
            /* The configuration size is stable: I'm done */
            //TIMER_STOP(log_fp);
            if (success_count > size / 2) {
                return RC_SUCCESS;  
            }
            else {
                return RC_INSUCCESS;
            }
        }
        j++;
    }
    //TIMER_STOP(log_fp);
    if (success_count > size / 2) {
        return RC_SUCCESS;  
    }
    
    return RC_INSUCCESS;
}

static int
wait_for_one( int *posted_sends, int qp_id )
{
    int rc, ne;
    int can_restart_qp;
    server_t *server;
    uint64_t wr_id;
    uint64_t *signaled_wrid_ptr;
    uint8_t  *qp_state_ptr;
    dare_ib_ep_t *ep;
    uint8_t i, k, size = get_group_size(SRV_DATA->config);
    uint8_t posted_send_count, success_count;
    uint8_t conn;
    
    /* Compute number of posted sends */
    for (i = 0, posted_send_count = 0, success_count = 0; i < size; i++) {
        if (posted_sends[i] < 0)
            continue;
        if (posted_sends[i] == 0) {
            success_count++;
        }
        posted_send_count += (uint8_t)posted_sends[i];
    }
    while ( (!success_count) && (posted_send_count) ) {
        ne = ibv_poll_cq(IBDEV->rc_cq[qp_id], IBDEV->rc_cqe, 
                    IBDEV->rc_wc_array);
        if (ne < 0) {
            /* Failure */
            error_return(RC_ERROR, log_fp, "Couldn't poll completion queue\n");
        }
        /* Check all completed WCs */
        for (i = 0; i < (uint8_t)ne; i++) {
            /* Define some temporary variables */
            wr_id = IBDEV->rc_wc_array[i].wr_id;
            conn = WRID_GET_CONN(wr_id);
            server = &SRV_DATA->config.servers[conn];                
            ep = (dare_ib_ep_t*)server->ep;
            //info_wtime(log_fp, "WR completed:"); PRINT_WRID_(wr_id);

            /* Check last get read ssn */
            if ( (LOG_QP == qp_id) && 
                (WRID_GET_SSN(wr_id) >= server->last_get_read_ssn) )
            {
                server->last_get_read_ssn = 0;
            }

            /* Check signaled WR */
            can_restart_qp = 1;
            signaled_wrid_ptr = &(ep->rc_ep.rc_qp[qp_id].signaled_wr_id);
            qp_state_ptr = &(ep->rc_ep.rc_qp[qp_id].state);
            if (0 != *signaled_wrid_ptr) {
                /* There is a signaled WR on this queue */
                if (wr_id == *signaled_wrid_ptr) {
                    /* Special signaled WR found - untag it */
                    WRID_UNSET_TAG(*signaled_wrid_ptr);
                    if (RC_QP_BLOCKED == *qp_state_ptr) {
                        /* Unblock QP */
                        *qp_state_ptr = RC_QP_ERROR;
                    }
                }
                else {
                    /* This WR is from the same QP as a signaled WR; 
                    in case of error, we cannot restart the QP to not lose the signaled WR */
                    can_restart_qp = 0;
                }
            }
           
#if 0           
            /* Check if this WR is a special signaled WR */
            can_restart_qp = 1;
            for (k = 0; k < size; k++) {
                /* Define some more temporary variables */
                ep = (dare_ib_ep_t*)SRV_DATA->config.servers[k].ep;
                signaled_wrid_ptr = &(ep->rc_ep.rc_qp[qp_id].signaled_wr_id);
                qp_state_ptr = &(ep->rc_ep.rc_qp[qp_id].state);
                
                if (0 == *signaled_wrid_ptr) continue;
                if (wr_id == *signaled_wrid_ptr) {
                    /* Special signaled WR found - untag */
                    WRID_UNSET_TAG(*signaled_wrid_ptr);   // unmark 
                    if (RC_QP_BLOCKED == *qp_state_ptr) {
                        /* Unblock QP */
                        *qp_state_ptr = RC_QP_ERROR;
                    }
                    break;
                }
                else if (WRID_GET_CONN(wr_id) == WRID_GET_CONN(*signaled_wrid_ptr)) {
                    /* This WR is from the same QP as a signaled WR; 
                    in case of error, we cannot restart the QP to not lose the signaled WR */
                    can_restart_qp = 0;
                }
            }
#endif            
            if (WRID_GET_SSN(wr_id) == ssn) {
                /* Decrement the number of sends posted in this round */
                posted_send_count--;
            }
            if (0 == ep->rc_connected) {
                /* This should never happen */
                continue;
            }
            
            /* Handle the WC */
            rc = handle_work_completion(&(IBDEV->rc_wc_array[0]), qp_id);
            handle_lr_work_completion(i, rc);
            if (WC_SUCCESS == rc) {
                /* Successful send operation: reset failure count */
                server->fail_count = 0;
                if (WRID_GET_SSN(wr_id) == ssn) {
                    /* Decrement the number of WC expected for this ep */
                    posted_sends[conn]--;
                    success_count++;
                }
                TIMER_INFO(log_fp, "      [+(p%"PRIu8",%"PRIu64")]\n", 
                           conn, WRID_GET_SSN(wr_id));            
                continue;
            }
            if (WC_FAILURE == rc) {
                /* The local QP moves into ERR state - restart it if possible 
                if not, the QP needs to be blocked since it's in the ERR state */
                if (can_restart_qp) {
                    /* Move QP into ERR state */
                    ep->rc_ep.rc_qp[qp_id].state = RC_QP_ERROR;
                }
                else {
                    /* To not lose the signaled WR, we restart the QP later;
                    however, we cannot post new WRs in this QP since it's in the ERR state*/
                    if (RC_QP_ERROR != ep->rc_ep.rc_qp[qp_id].state)  {
  //                  info_wtime(log_fp, "[%s:%d] Blocking QP %d of p%"PRIu8"\n", 
  //                          __func__, __LINE__, qp_id, conn);
                        ep->rc_ep.rc_qp[qp_id].state = RC_QP_BLOCKED;
                    }
                }
                /* Operation fail: increase failure count */
                if (CTRL_QP == qp_id) {
                    server->fail_count++;
                }
                TIMER_INFO(log_fp, "      [-(p%"PRIu8",%"PRIu64")]\n",  
                           conn, WRID_GET_SSN(wr_id));
                continue;
            }
            if (WC_SOFTWARE_BUG == rc) {
                /* WR failed due to a software bug */
                debug(log_fp, "Software bug!!!!!\n");
                continue;
            }
            if (WC_ERROR == rc) {
                // TODO: find out the error for QP inaccessible
            }
        }
    }
    if (success_count) {
        return RC_SUCCESS;  
    }
    return RC_INSUCCESS;
}

/**
 *  Handle the WC for a log replication WR posted for server idx
 */
static void
handle_lr_work_completion( uint8_t idx, int wc_rc )
{
    uint64_t wr_id = IBDEV->rc_wc_array[idx].wr_id;
    WRID_UNSET_TAG(wr_id);
    WRID_UNSET_WA(wr_id); 
    idx = WRID_GET_CONN(wr_id);
    server_t *server = &SRV_DATA->config.servers[idx];
    
//info_wtime(log_fp, "lr_work_completion: p%"PRIu8"\n", idx);
    if (wr_id == server->next_wr_id) {
        if (WC_SUCCESS == wc_rc) {        
            if (server->next_lr_step == LR_UPDATE_LOG) {
                /* Current LR step is update log */
                switch (server->send_count) {
                    case 0: 
                        /* The other part of the send failed */
                        server->send_flag = 1;
                        break;
                    case 1:
                        /* The log was updated successfully */
                        server->next_lr_step = LR_UPDATE_END;
//TIMER_INFO(log_fp, "[p%"PRIu8"->log(:%"PRIu64")]\n", idx, server->cached_end_offset);
                        server->send_flag = 1;
                        break;
                    case 2:
                        /* First part of the send */
                        server->send_count--;
                        break;
                }
            }
            else if (server->next_lr_step != LR_UPDATE_END) {
                /* Current LR step succeeded */
                server->next_lr_step++;
//TIMER_INFO(log_fp, "[p%"PRIu8"->S%"PRIu8"(e=%"PRIu64")]\n", idx, server->next_lr_step, SRV_DATA->ctrl_data->log_offsets[idx].end);
                server->send_flag = 1;
            }
            else {
                /* Successful end offset update */
                server->next_lr_step = LR_UPDATE_LOG;
//TIMER_INFO(log_fp, "[p%"PRIu8"->e=%"PRIu64"]\n", idx, SRV_DATA->ctrl_data->log_offsets[idx].end);
                server->send_flag = 1;
            }
        }
        else {
            if (server->next_lr_step == LR_UPDATE_LOG) {
                /* Current LR step is update log */
                switch (server->send_count) {
                    case 0:
                    case 1:
                        /* The update was unsuccessful */
                        server->send_flag = 1;
                        break;
                    case 2:
                        /* The first part of the update was unsuccessful */
                        server->send_count = 0;
                        break;
                }
            }
            else if (server->next_lr_step != LR_UPDATE_END) {
                /* Current LR step failed */
                server->send_flag = 1;
            }
            else {
                /* Unsuccessful end offset update */
                //server->next_lr_step = LR_UPDATE_LOG;
                server->send_flag = 1;
            }
        }
    }
}


/**
 * Handle the completion status of a WC 
 */
static int
handle_work_completion( struct ibv_wc *wc, int qp_id )
{
    int rc;
    uint64_t wr_id = wc->wr_id;
    uint8_t wr_idx = WRID_GET_CONN(wr_id);
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[wr_idx].ep;

    /* Verify completion status */
    switch(wc->status) {
        case IBV_WC_SUCCESS:
            /* IBV_WC_SUCCESS: Operation completed successfully */
            return WC_SUCCESS;
        case IBV_WC_LOC_LEN_ERR:    //  Local Length Error
        case IBV_WC_LOC_QP_OP_ERR:  //  Local QP Operation Error
        case IBV_WC_LOC_EEC_OP_ERR: //  Local EE Context Operation Error
        case IBV_WC_LOC_PROT_ERR:   //  Local Protection Error   
        case IBV_WC_MW_BIND_ERR:    //  Memory Window Binding Error
        case IBV_WC_LOC_ACCESS_ERR: //  Local Access Error
        case IBV_WC_REM_ACCESS_ERR: //  Remote Access Error
        case IBV_WC_RNR_RETRY_EXC_ERR:  // RNR Retry Counter Exceeded
        case IBV_WC_LOC_RDD_VIOL_ERR:   // Local RDD Violation Error
        case IBV_WC_REM_INV_RD_REQ_ERR: // Remote Invalid RD Request
        case IBV_WC_REM_ABORT_ERR:  // Remote Aborted Error
        case IBV_WC_INV_EECN_ERR:   // Invalid EE Context Number
        case IBV_WC_INV_EEC_STATE_ERR:  // Invalid EE Context State Error
            /* Nothing to do with the failure of the remote side; 
               most likely a software bug */
            info(log_fp, 
                 "\nsoftware bug: WC for (lid=%"PRIu16") has status %s (%d)\n",
                 ep->ud_ep.lid, ibv_wc_status_str(wc->status), wc->status);
            rc = WC_SOFTWARE_BUG; terminate = 1;
            break;
        case IBV_WC_WR_FLUSH_ERR:
            /* Work Request Flushed Error: A Work Request was in 
            process or outstanding when the QP transitioned into the 
            Error State. */
            text(log_fp, "\nIBV_WC_WR_FLUSH_ERR\n");  
            //info_wtime(log_fp, "IBV_WC_WR_FLUSH_ERR on p%"PRIu8" %s QP\n", wr_idx, qp_id == LOG_QP ? "LOG" : "CTRL"); 
            rc = WC_FAILURE;
            /* The local QP moves into ERR state - restart it */
            //rc_qp_restart(ep, qp_id);
            break;
        case IBV_WC_BAD_RESP_ERR:
            /* Bad Response Error - an unexpected transport layer 
            opcode was returned by the responder. */
            text(log_fp, "\nIBV_WC_BAD_RESP_ERR\n"); 
            rc = WC_ERROR;
            break;
        case IBV_WC_REM_INV_REQ_ERR:
            /* Remote Invalid Request Error: The responder detected an 
            invalid message on the channel. Possible causes include the 
            operation is not supported by this receive queue, insufficient 
            buffering to receive a new RDMA or Atomic Operation request, 
            or the length specified in an RDMA request is greater than 
            2^{31} bytes. Relevant for RC QPs. */
            text(log_fp, "\nIBV_WC_REM_INV_REQ_ERR\n");
            rc = WC_ERROR; 
            break;
        case IBV_WC_REM_OP_ERR:
            /* Remote Operation Error: the operation could not be 
            completed successfully by the responder. Possible causes 
            include a responder QP related error that prevented the 
            responder from completing the request or a malformed WQE on 
            the Receive Queue. Relevant for RC QPs. */
            text(log_fp, "\nIBV_WC_REM_INV_REQ_ERR\n"); 
            rc = WC_ERROR;
            break;
        case IBV_WC_RETRY_EXC_ERR:
            /* Transport Retry Counter Exceeded: The local transport 
            timeout retry counter was exceeded while trying to send this 
            message. This means that the remote side didn’t send any Ack 
            or Nack. If this happens when sending the first message, 
            usually this mean that the connection attributes are wrong or 
            the remote side isn’t in a state that it can respond to messages. 
            If this happens after sending the first message, usually it 
            means that the remote QP isn’t available anymore. */
            /* REMOTE SIDE IS DOWN */
            text(log_fp, "\nIBV_WC_RETRY_EXC_ERR\n"); 
            info_wtime(log_fp, "IBV_WC_RETRY_EXC_ERR on p%"PRIu8" %s QP\n", wr_idx, qp_id == LOG_QP ? "LOG" : "CTRL"); 
            rc = WC_FAILURE;
            /* The local QP moves into ERR state - restart it */
            //rc_qp_restart(ep, qp_id);
            break;
        case IBV_WC_FATAL_ERR:
            /* Fatal Error - WTF */
            text(log_fp, "\nIBV_WC_FATAL_ERR\n"); 
            rc = WC_ERROR;
            break;
        case IBV_WC_RESP_TIMEOUT_ERR:
            /* Response Timeout Error */
            text(log_fp, "\nIBV_WC_RESP_TIMEOUT_ERR\n"); 
            rc = WC_ERROR;
            break;
        case IBV_WC_GENERAL_ERR:
            /* General Error: other error which isn’t one of the above errors. */
            text(log_fp, "\nIBV_WC_GENERAL_ERR\n"); 
            rc = WC_ERROR;
            break;
    }

#if DEBUG
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    ibv_query_qp(ep->rc_ep.rc_qp[qp_id].qp, &attr, IBV_QP_STATE, &init_attr);
    info(log_fp, "[QP Info (LID=%"PRIu16")] %s QP: %s\n",
         ep->ud_ep.lid, (LOG_QP == qp_id) ? "log" : "ctrl",
         qp_state_to_str(attr.qp_state));
//    print_rc_info();
#endif         
    //dare_ib_print_ud_qp();
    return rc; 
}

#endif

/* ================================================================== */
/* LogGP */
#if 1

# define MEASURE_COUNT 1000
double rc_get_loggp_params( uint32_t size, 
                            int type, 
                            int *poll_count, 
                            int write,
                            int inline_flag )
{
    int rc, i, count, ne;
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;
    uint8_t target = !SRV_DATA->config.idx;
    int posted_sends[MAX_SERVER_COUNT];
    for (i = 0; i < MAX_SERVER_COUNT; i++) {
        posted_sends[i] = -1;
    }
    
    HRT_TIMESTAMP_T t1, t2,t3,t4;
    uint64_t ticks[MEASURE_COUNT];
    uint64_t poll_ticks[MEASURE_COUNT];
    uint64_t poll_counts[MEASURE_COUNT];
    double usecs; 
    
    if (size > SRV_DATA->log->len) {
        error_return(0, log_fp, "Maximum buffer size for LogGP RC is %"PRIu64"\n", 
                    SRV_DATA->log->len);
    }
    loggp_not_inline = !inline_flag;
    
    /* Set remote memory region */
    rem_mem_t rm;
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[target].ep;
    uint32_t offset = (uint32_t) (offsetof(dare_log_t, entries));
    rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset + 123;
    rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;
    
    if (LOGGP_PARAM_O == type) {
        //if (write) {
            count = IBDEV->ib_dev_attr.max_qp_wr/2;
            /* Measure overhead of posting send operations */
            HRT_GET_TIMESTAMP(t1);
        //}
        //else {
            count = IBDEV->ib_dev_attr.max_qp_rd_atom;
            *poll_count = 0;
        //}
    }
    else {
        count = MEASURE_COUNT;
    }
loop:    
    //if (!write && (LOGGP_PARAM_O == type) ) {
    if ((LOGGP_PARAM_O == type) ) {
        /* Measure overhead of posting send operations */
        HRT_GET_TIMESTAMP(t1);
    }
    for (i = 0; i < count; i++) {
        if (LOGGP_PARAM_OPX == type) {
            /* Measure overhead of polling with no result */
            HRT_GET_TIMESTAMP(t1);
            poll_counts[i] = 0;
            while (poll_counts[i] < *poll_count) {
                poll_counts[i]++;
                ne = ibv_poll_cq(IBDEV->rc_cq[LOG_QP], 1, IBDEV->rc_wc_array);
                if (ne < 0) {
                    error_return(0, log_fp, "ibv_poll_cq() failed\n");
                }
            }
            HRT_GET_TIMESTAMP(t2);
            HRT_GET_ELAPSED_TICKS(t1, t2, &ticks[i]);
            continue;
        }
        if (LOGGP_PARAM_L == type) {
            /* Measure combined latency of send operations */
            HRT_GET_TIMESTAMP(t1);
        }
#if 1
        if (LOGGP_PARAM_O == type) {
            /* Post send operation */
            ssn++;
            posted_sends[target]=1;
            /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
            rc = post_send(target, LOG_QP, SRV_DATA->log->entries, size, 
                    IBDEV->lcl_mr[LOG_QP], (write ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ), 
                    SIGNALED, rm, posted_sends);
        }
        else if (LOGGP_PARAM_OP == type) {
#endif            
            memset(&sg, 0, sizeof(sg));
            sg.addr   = (uint64_t)SRV_DATA->log->entries;
            sg.length = size;
            sg.lkey   = IBDEV->lcl_mr[LOG_QP]->lkey;        
            memset(&wr, 0, sizeof(wr));
            wr.wr_id = 1;
            wr.sg_list    = &sg;
            wr.num_sge    = 1;
            wr.opcode     = (write ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ);
            wr.send_flags |= IBV_SEND_SIGNALED;
            if ( write && inline_flag && (size <= IBDEV->rc_max_inline_data) ) {
                wr.send_flags |= IBV_SEND_INLINE;
            }
            wr.wr.rdma.remote_addr = rm.raddr;
            wr.wr.rdma.rkey        = rm.rkey;
            rc = ibv_post_send(ep->rc_ep.rc_qp[LOG_QP].qp, &wr, &bad_wr);
            if (0 != rc) {
                error_return(0, log_fp, "ibv_post_send failed because %s [%s]\n", 
                    strerror(rc), rc == EINVAL ? "EINVAL" : rc == ENOMEM ? 
                    "ENOMEM" : rc == EFAULT ? "EFAULT" : "UNKNOWN");
            }
//        if (LOGGP_PARAM_OP == type) {
            /* Wait for 1 ms */
            usecs_wait(1000);
            
            /* Measure overhead of polling for completion */
            HRT_GET_TIMESTAMP(t1);           
            ne = ibv_poll_cq(IBDEV->rc_cq[LOG_QP], 1, IBDEV->rc_wc_array);
            if (0 == ne) {
                error_return(0, log_fp, "Increase time\n");
            }
            if (ne < 0) {
                error_return(0, log_fp, "ibv_poll_cq() failed\n");
            }
        }        
        else if (LOGGP_PARAM_L == type) {
#if 1
            ssn++;
            posted_sends[target]=1;
            /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
            rc = post_send(target, LOG_QP, SRV_DATA->log->entries+size, size, 
                    IBDEV->lcl_mr[LOG_QP], (write ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ), 
                    SIGNALED, rm, posted_sends);
            rc = wait_for_one(posted_sends, LOG_QP);
            if (RC_ERROR == rc) {
                /* This should never happen */
                error_return(0, log_fp, "Cannot get log entries\n");
            }
            if (RC_SUCCESS != rc) {
                /* Operation failed; try again later */
                error_return(0, log_fp, "RDMA op failed\n");
            }
            poll_counts[i] = 0;
#else            
            /* Wait for completion */
            poll_counts[i] = 0;
            //HRT_GET_TIMESTAMP(t3);
            while (1) {
                poll_counts[i]++;
                ne = ibv_poll_cq(IBDEV->rc_cq[LOG_QP], 1, IBDEV->rc_wc_array);
                if (0 == ne) {
                    continue;
                }
                if (ne < 0) {
                    error_return(0, log_fp, "ibv_poll_cq() failed\n");
                }
                break;
            }
            //HRT_GET_TIMESTAMP(t4);
            //HRT_GET_ELAPSED_TICKS(t3, t4, &poll_ticks[i]);
#endif            
        }
        if (LOGGP_PARAM_O != type) {
            HRT_GET_TIMESTAMP(t2);
            HRT_GET_ELAPSED_TICKS(t1, t2, &ticks[i]);
        }
    }
    //if ( !write && (LOGGP_PARAM_O == type) ) {
    if (  (LOGGP_PARAM_O == type) ) {
        HRT_GET_TIMESTAMP(t2);
        HRT_GET_ELAPSED_TICKS(t1, t2, &ticks[*poll_count]);
        (*poll_count)++;
        if (*poll_count != MEASURE_COUNT) {
            wait_for_one(posted_sends, LOG_QP);
            goto loop;
        }
    }
    if (LOGGP_PARAM_OPX == type) {
        /* Get median mean */
        qsort(ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
        usecs = HRT_GET_USEC(ticks[MEASURE_COUNT/2]) / *poll_count;
    }
    else if (LOGGP_PARAM_O == type) {
        //if (write) {
        if (0) {
            /* Get mean */
            HRT_GET_TIMESTAMP(t2);
            HRT_GET_ELAPSED_TICKS(t1, t2, ticks);
            usecs = HRT_GET_USEC(*ticks) / count;
        }
        //else {
            /* Get median mean */
            qsort(ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
            usecs = HRT_GET_USEC(ticks[MEASURE_COUNT/2]) / count;
            info(log_fp, "(%lf; %lf)\n", HRT_GET_USEC(ticks[0]) / count, HRT_GET_USEC(ticks[MEASURE_COUNT-1]) / count);
        //}
        
        /* Clear completion queue */
#if 1        
        ssn++;
        posted_sends[target]=1;
        /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */ 
        rc = post_send(target, LOG_QP, SRV_DATA->log->entries, size, 
                IBDEV->lcl_mr[LOG_QP], (write ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ), 
                SIGNALED, rm, posted_sends);
        rc = wait_for_one(posted_sends, LOG_QP);
        if (RC_ERROR == rc) {
            /* This should never happen */
            error_return(0, log_fp, "Cannot get log entries\n");
        }
        if (RC_SUCCESS != rc) {
            /* Operation failed; try again later */
            error_return(0, log_fp, "RDMA op failed\n");
        }
#else 
        memset(&sg, 0, sizeof(sg));
        sg.addr   = (uint64_t)SRV_DATA->log->entries;
        sg.length = size;
        sg.lkey   = IBDEV->lcl_mr[LOG_QP]->lkey;
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = 13;
        wr.sg_list    = &sg;
        wr.num_sge    = 1;
        wr.opcode     = IBV_WR_RDMA_WRITE;
        wr.send_flags |= IBV_SEND_SIGNALED;
        if (size <= IBDEV->rc_max_inline_data) {
            wr.send_flags |= IBV_SEND_INLINE;
        }
        wr.wr.rdma.remote_addr = rm.raddr;
        wr.wr.rdma.rkey        = rm.rkey;
        rc = ibv_post_send(ep->rc_ep.rc_qp[LOG_QP].qp, &wr, &bad_wr);
        if (0 != rc) {
            error_return(0, log_fp, "ibv_post_send failed because %s [%s]\n", 
                strerror(rc), rc == EINVAL ? "EINVAL" : rc == ENOMEM ? 
                "ENOMEM" : rc == EFAULT ? "EFAULT" : "UNKNOWN");
        }
        while (1) {
            ne = ibv_poll_cq(IBDEV->rc_cq[LOG_QP], 1, IBDEV->rc_wc_array);
            if (0 == ne) {
                continue;
            }
            if (ne < 0) {
                error_return(0, log_fp, "ibv_poll_cq() failed\n");
            }
            if (13 == IBDEV->rc_wc_array[0].wr_id) {
                break;
            }
        }
#endif        
    }
    else {
        /* Get median */
        qsort(ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
        usecs = HRT_GET_USEC(ticks[MEASURE_COUNT/2]);
        info(log_fp, "\n(%lf; %lf)\n", HRT_GET_USEC(ticks[49]), HRT_GET_USEC(ticks[949]));
        qsort(poll_counts, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
        if (LOGGP_PARAM_L == type) {
            *poll_count = (int)poll_counts[MEASURE_COUNT/2];
            //qsort(poll_ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
            //info(log_fp, "   # poll[%"PRIu32"]=%lf; poll_count=%d\n", size, HRT_GET_USEC(poll_ticks[MEASURE_COUNT/2]), *poll_count);
        //info(log_fp, "   # ticks_min=%"PRIu64"; ticks_max=%"PRIu64"\n", ticks[0], ticks[MEASURE_COUNT-1]);
        //info(log_fp, "   # poll_ticks_min=%"PRIu64"; poll_ticks_max=%"PRIu64"\n", poll_ticks[0], poll_ticks[MEASURE_COUNT-1]);
        //info(log_fp, "   # poll_counts_min=%"PRIu64"; poll_counts_max=%"PRIu64"\n", poll_counts[0], poll_counts[MEASURE_COUNT-1]);
        }
#if 0        
        else {
            qsort(poll_ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
            usecs = HRT_GET_USEC(poll_ticks[MEASURE_COUNT/2]);
            usecs -= (poll_counts[MEASURE_COUNT/2] - 1) * SRV_DATA->loggp.o_poll_x; 
            //usecs = HRT_GET_USEC(poll_ticks[MEASURE_COUNT/2]/poll_counts[MEASURE_COUNT/2]);
        info(log_fp, "   # ticks_min=%"PRIu64"; ticks_max=%"PRIu64"\n", poll_ticks[0], poll_ticks[MEASURE_COUNT-1]);
        info(log_fp, "   # ticks_min=%"PRIu64"; ticks_max=%"PRIu64"\n", poll_ticks[1], poll_ticks[MEASURE_COUNT-2]);
        }
#endif         
    }

    return usecs;
}

double rc_loggp_prtt( int n, double delay, uint32_t size )
{
    int rc, i, j, count, ne;
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;
    uint8_t target = !SRV_DATA->config.idx;
    
    HRT_TIMESTAMP_T t1, t2;
    uint64_t ticks[MEASURE_COUNT];
    double usecs;
    
    if (size > SRV_DATA->log->len) {
        error_return(0, log_fp, "Maximum buffer size for LogGP is %"PRIu64"\n", 
                    SRV_DATA->log->len);
    }
#if 0   
    for (j = 0; j < MEASURE_COUNT; j++) {
        HRT_GET_TIMESTAMP(t1);
        usecs_wait(delay);
        HRT_GET_TIMESTAMP(t2);
        HRT_GET_ELAPSED_TICKS(t1, t2, &ticks[j]);
    }
    qsort(ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
    usecs = HRT_GET_USEC(ticks[MEASURE_COUNT/2]);
    info(log_fp, "Wait for %lf = %lf (%lf, %lf)\n", delay, usecs, HRT_GET_USEC(ticks[0]), HRT_GET_USEC(ticks[MEASURE_COUNT-1]));
#endif
    
    /* Set remote memory region */
    rem_mem_t rm;
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[target].ep;
    uint32_t offset = (uint32_t) (offsetof(dare_log_t, entries));
    rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
    rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey; 
    
    for (j = 0; j < MEASURE_COUNT; j++) {
        HRT_GET_TIMESTAMP(t1);
        for (i = 0; i < n - 1; i++) {
            memset(&sg, 0, sizeof(sg));
            sg.addr   = (uint64_t)SRV_DATA->log->entries;
            sg.length = size;
            sg.lkey   = IBDEV->lcl_mr[LOG_QP]->lkey;        
            memset(&wr, 0, sizeof(wr));
            wr.wr_id = (uint64_t)i;
            wr.sg_list    = &sg;
            wr.num_sge    = 1;
            wr.opcode     = IBV_WR_RDMA_WRITE;
            //wr.send_flags |= IBV_SEND_SIGNALED;
            if (size <= IBDEV->rc_max_inline_data) {
                wr.send_flags |= IBV_SEND_INLINE;
            }
            wr.wr.rdma.remote_addr = rm.raddr;
            wr.wr.rdma.rkey        = rm.rkey;
            rc = ibv_post_send(ep->rc_ep.rc_qp[LOG_QP].qp, &wr, &bad_wr);
            if (0 != rc) {
                error_return(0, log_fp, "ibv_post_send failed because %s [%s]\n", 
                    strerror(rc), rc == EINVAL ? "EINVAL" : rc == ENOMEM ? 
                    "ENOMEM" : rc == EFAULT ? "EFAULT" : "UNKNOWN");
            }
            /* Wait delay ms */
            usecs_wait(delay);
        }
        
        memset(&sg, 0, sizeof(sg));
        sg.addr   = (uint64_t)SRV_DATA->log->entries;
        sg.length = size;
        sg.lkey   = IBDEV->lcl_mr[LOG_QP]->lkey;        
        memset(&wr, 0, sizeof(wr));
        wr.wr_id = (uint64_t)n;
        wr.sg_list    = &sg;
        wr.num_sge    = 1;
        wr.opcode     = IBV_WR_RDMA_WRITE;
        wr.send_flags |= IBV_SEND_SIGNALED;
        if (size <= IBDEV->rc_max_inline_data) {
            wr.send_flags |= IBV_SEND_INLINE;
        }
        wr.wr.rdma.remote_addr = rm.raddr;
        wr.wr.rdma.rkey        = rm.rkey;
        rc = ibv_post_send(ep->rc_ep.rc_qp[LOG_QP].qp, &wr, &bad_wr);
        if (0 != rc) {
            error_return(0, log_fp, "ibv_post_send failed because %s [%s]\n", 
                strerror(rc), rc == EINVAL ? "EINVAL" : rc == ENOMEM ? 
                "ENOMEM" : rc == EFAULT ? "EFAULT" : "UNKNOWN");
        }
        
        while (1) {
            ne = ibv_poll_cq(IBDEV->rc_cq[LOG_QP], 1, IBDEV->rc_wc_array);
            if (0 == ne) {
                continue;
            }
            if (ne < 0) {
                error_return(0, log_fp, "ibv_poll_cq() failed\n");
            }
            if ((uint64_t)n == IBDEV->rc_wc_array[0].wr_id) {
                break;
            }
        }
        HRT_GET_TIMESTAMP(t2);
        HRT_GET_ELAPSED_TICKS(t1, t2, &ticks[j]);
    }
    qsort(ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
    usecs = HRT_GET_USEC(ticks[MEASURE_COUNT/2]);
    return usecs;
}

int rc_loggp_exit()
{
    int rc, count, ne;
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr;
    uint8_t target = !SRV_DATA->config.idx;
    
    SRV_DATA->log->entries[0] = 13;
    
    rem_mem_t rm;
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[target].ep;
    uint32_t offset = (uint32_t) (offsetof(dare_log_t, entries));
    rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
    rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;
    
    memset(&sg, 0, sizeof(sg));
    sg.addr   = (uint64_t)SRV_DATA->log->entries;
    sg.length = 8;
    sg.lkey   = IBDEV->lcl_mr[LOG_QP]->lkey;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = 1;
    wr.sg_list    = &sg;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_RDMA_WRITE;
    wr.send_flags |= IBV_SEND_INLINE;
    wr.wr.rdma.remote_addr = rm.raddr;
    wr.wr.rdma.rkey        = rm.rkey;
    rc = ibv_post_send(ep->rc_ep.rc_qp[LOG_QP].qp, &wr, &bad_wr);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_post_send failed because %s [%s]\n", 
            strerror(rc), rc == EINVAL ? "EINVAL" : rc == ENOMEM ? 
            "ENOMEM" : rc == EFAULT ? "EFAULT" : "UNKNOWN");
    }
    return 0;
}

static int 
cmpfunc_uint64( const void *a, const void *b )
{
    return ( *(uint64_t*)a - *(uint64_t*)b );
}


#endif 

void rc_ib_send_msg() 
{
    uint8_t target = 1;
    if (SRV_DATA->config.idx == 1) {
        struct ibv_qp_attr attr;
        struct ibv_qp_init_attr init_attr;
        static uint32_t rc_psn0 = 20;
        static uint32_t rc_psn2 = 20;
        dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[0].ep;
        dare_ib_ep_t *ep2 = (dare_ib_ep_t*)SRV_DATA->config.servers[2].ep;
        ibv_query_qp(ep->rc_ep.rc_qp[LOG_QP].qp, &attr, IBV_QP_RQ_PSN, &init_attr);
        if (rc_psn0 != attr.rq_psn) {
            info_wtime(log_fp, "RQ_PSN = %"PRIu32"\n", attr.rq_psn);
            rc_psn0 = attr.rq_psn;
        }
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[2].ep;
        ibv_query_qp(ep->rc_ep.rc_qp[LOG_QP].qp, &attr, IBV_QP_RQ_PSN, &init_attr);
        if (rc_psn2 != attr.rq_psn) {
            info_wtime(log_fp, "RQ_PSN = %"PRIu32"\n", attr.rq_psn);
            rc_psn2 = attr.rq_psn;
        }
        return;

    }
    int rc, i, posted_sends[MAX_SERVER_COUNT];
    for (i = 0; i < MAX_SERVER_COUNT; i++) {
        posted_sends[i] = -1;
    }
    
    /* Set remote memory region */
    rem_mem_t rm;
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[target].ep;
    uint32_t offset = (uint32_t) (offsetof(dare_log_t, entries));
    rm.raddr = ep->rc_ep.rmt_mr[LOG_QP].raddr + offset;
    rm.rkey = ep->rc_ep.rmt_mr[LOG_QP].rkey;

    /* server_id, qp_id, buf, len, mr, opcode, signaled, rm, posted_sends */
    ssn++;
    posted_sends[target] = 1;
    post_send(target, LOG_QP, SRV_DATA->log->entries, sizeof(uint64_t), 
                    IBDEV->lcl_mr[LOG_QP], IBV_WR_RDMA_WRITE, 
                    SIGNALED, rm, posted_sends);
    rc = wait_for_one(posted_sends, LOG_QP);
    if (RC_ERROR == rc) {
        error(log_fp, "Cannot get log entries\n");
    }
    if (RC_SUCCESS != rc) {
        info_wtime(log_fp, "Operation failed\n");
    }
    else {
        info_wtime(log_fp, "Operation succeded\n");
    }
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    ibv_query_qp(ep->rc_ep.rc_qp[LOG_QP].qp, &attr, IBV_QP_SQ_PSN, &init_attr);
    info_wtime(log_fp, "SQ_PSN = %"PRIu32"\n", attr.sq_psn);
}
