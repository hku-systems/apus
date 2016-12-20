/**                                                                                                      
 * DARE (Direct Access REplication)
 * 
 * Network module for the DARE RSM algorithm (IB verbs)
 * Note: these functions act mainly as interfaces 
 * to the RC or UD IB modules (see dare_ibv_ud.c & dare_ibv_rc.c)
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <netinet/in.h>
#include <time.h>
 
#include <dare.h>
#include <dare_ibv.h>
#include <dare_ibv_ud.h>
#include <dare_ibv_rc.h>
#include <dare_server.h>
#include <dare_client.h>

extern FILE *log_fp;

/* InfiniBand device */
dare_ib_device_t *dare_ib_device;
#define IBDEV dare_ib_device
#define SRV_DATA ((dare_server_data_t*)dare_ib_device->udata)
#define CLT_DATA ((dare_client_data_t*)dare_ib_device->udata)

/* ================================================================== */
/* local function - prototypes */

static dare_ib_device_t* 
init_one_device( struct ibv_device* ib_dev );
static void 
free_ib_device();
static int
test_qp_support_rc( struct ibv_context *device_context );

/* ================================================================== */
/* Init and cleaning up */
#if 1

/** 
 * Initialize the IB device 
 *  - ibv_device; ibv_context & ud_init()
 *  Note: both servers and clients need this step
 */
int dare_init_ib_device( uint32_t receive_count )
{
    int i;
    int num_devs;
    struct ibv_device **ib_devs = NULL;
    
    /* Get list of devices (HCAs) */ 
    ib_devs = ibv_get_device_list(&num_devs);
    if (0 == num_devs) {
        error_return(1, log_fp, "No HCAs available\n");
    }
    if (NULL == ib_devs) {
        error_return(1, log_fp, "Get device list returned NULL\n");
    }   
    
    /* Go through all devices and find one that we can use */
    for (i = 0; i < num_devs; i++) {
        /* Init device */
        IBDEV = init_one_device(ib_devs[i]);
        if (NULL != IBDEV) {
            /* Found it */
            break;
        }
    }
    
    /* Free device list */
    ibv_free_device_list(ib_devs);
    
    if (NULL == IBDEV) {
        /* Cannot find device */
        return 1;
    }
    
    /* Initialize IB UD connection */
    ud_init(receive_count);

    return 0; 
}

/**
 * Post UD receives
 * Note: After this call, we may receive messages over UD
 */
int dare_start_ib_ud()
{
    return ud_start();
}

int dare_init_ib_srv_data( void *data )
{
    int i;
    IBDEV->udata = data;
    IBDEV->ulp_type = IBV_SERVER;
     
    for (i = 0; i < SRV_DATA->config.len; i++) {
        SRV_DATA->config.servers[i].ep = malloc(sizeof(dare_ib_ep_t));
        if (NULL == SRV_DATA->config.servers[i].ep) {
            error_return(1, log_fp, "Cannot allocate EP\n");
        }
        memset(SRV_DATA->config.servers[i].ep, 0, sizeof(dare_ib_ep_t));
    }
    
    return 0;
}

int dare_init_ib_clt_data( void *data )
{
    IBDEV->udata = data;
    IBDEV->ulp_type = IBV_CLIENT;
    return 0;
}

/** 
 * Init IB reliable connection (RC)
 */
int dare_init_ib_rc()
{
    return rc_init();
}

static dare_ib_device_t* 
init_one_device( struct ibv_device* ib_dev )
{
    int i;
    dare_ib_device_t *device = NULL;
    struct ibv_context *dev_context = NULL;
    
    /* Open up the device */
    dev_context = ibv_open_device(ib_dev);
    if (NULL == dev_context) {
        goto error;
    }
    
    /* Find out if this device supports RC QPs */
    if(test_qp_support_rc(dev_context)) {
        goto error;
    }
    
    /* Allocate new device */
    device = (dare_ib_device_t*)malloc(sizeof(dare_ib_device_t));
    if (NULL == device) {
        goto error;
    }
    memset(device, 0, sizeof(dare_ib_device_t));
    
    /* Init device */
    device->ib_dev = ib_dev;
    device->ib_dev_context = dev_context;
    device->request_id = 1;
    
    /* Get device's attributes */
    if(ibv_query_device(device->ib_dev_context, &device->ib_dev_attr)){
        goto error;
    }
    
    if (IBV_ATOMIC_NONE == device->ib_dev_attr.atomic_cap) {
        info(log_fp, "# HCA %s does not support atomic operations\n", 
             ibv_get_device_name(device->ib_dev));
    }
    else {
        info(log_fp, "# HCA %s supports atomic operations\n", 
             ibv_get_device_name(device->ib_dev));
    }
    info(log_fp, "# max_qp_wr=%d\n", device->ib_dev_attr.max_qp_wr);
    info(log_fp, "# max_qp_rd_atom=%d\n", device->ib_dev_attr.max_qp_rd_atom);
    
    if (0 == device->ib_dev_attr.max_srq) {
        info(log_fp, "# HCA %s does not support Shared Receive Queues.\n", 
             ibv_get_device_name(device->ib_dev));
    }
    else {
        info(log_fp, "# HCA %s supports Shared Receive Queues.\n",
             ibv_get_device_name(device->ib_dev));
    }
    
    if (0 == device->ib_dev_attr.max_mcast_grp) {
        info(log_fp, "# HCA %s does not support multicast groups.\n", 
             ibv_get_device_name(device->ib_dev));
    }
    else {
        info(log_fp, "# HCA %s supports multicast groups.\n",
             ibv_get_device_name(device->ib_dev));
    }
    
    info(log_fp, "# HCA %s supports maximum %d WRs.\n", 
             ibv_get_device_name(device->ib_dev), device->ib_dev_attr.max_qp_wr);
    

    /* Find port */
    device->port_num = 0;
    for (i = 1; i <= device->ib_dev_attr.phys_port_cnt; i++) {
        struct ibv_port_attr ib_port_attr;
        if (ibv_query_port(device->ib_dev_context, i, &ib_port_attr)) {
            goto error;
        }
        if (IBV_PORT_ACTIVE != ib_port_attr.state) {
            continue;
        }

        /* find index of pkey 0xFFFF */
        uint16_t pkey, j;
        for (j = 0; j < device->ib_dev_attr.max_pkeys; j++) {
            if (ibv_query_pkey(device->ib_dev_context, i, j, &pkey)) {
                goto error;
            }
            pkey = ntohs(pkey);// & IB_PKEY_MASK;
            info(log_fp, "# pkey_value = %"PRIu16" for index %"PRIu16"\n",
                 pkey, j);
            if (pkey == 0xFFFF) {
                device->pkey_index = j;
                break;
            }
        }
        device->mtu = ib_port_attr.max_mtu;
        //device->mtu = ib_port_attr.active_mtu;
        //device->mtu = IBV_MTU_2048;
        info(log_fp, "# ib_port_attr.active_mtu = %"PRIu32" (%d bytes)\n", 
             ib_port_attr.active_mtu, mtu_value(ib_port_attr.active_mtu));
        info(log_fp, "# device->mtu = %"PRIu32" (%d bytes)\n", 
             device->mtu, mtu_value(device->mtu));
 //       info(log_fp, "# ib_port_attr.lmc = %"PRIu8"\n", ib_port_attr.lmc);

        device->port_num = i;
        info(log_fp, "# device->port_num = %"PRIu8"\n", device->port_num);
        device->lid = ib_port_attr.lid;
        info(log_fp, "# ib_port_attr.lid = %"PRIu16"\n", ib_port_attr.lid);
        
        break;
    }
    if (0 == device->port_num) {
        goto error;
    }
      
    return device;

error:
    /* Free device */
    if (NULL != device) {
        free(device);
    }
    /* Free the device context */
    if (NULL != dev_context) {
        ibv_close_device(dev_context);
    }
    return NULL;      
}

void dare_ib_destroy_ep( uint8_t idx )
{
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[idx].ep;
    if (NULL == ep) {
        return;
    }
    ud_ah_destroy(ep->ud_ep.ah);
    // Note: RC QPs are already destroyed in rc_free
    free(ep);
    SRV_DATA->config.servers[idx].ep = NULL;
}

/**
 * Cleanup server
 */
void dare_ib_srv_shutdown()
{
    /* Free RC resources */
    rc_free();
        
    /* Free UD endpoints */
    if (NULL != SRV_DATA) {
        if (NULL != SRV_DATA->config.servers) {
            uint8_t i, size = SRV_DATA->config.len;
            for (i = 0; i < size; i++) {
                dare_ib_destroy_ep(i);
            }
        }
    }
    
    free_ib_device();
}

/**
 * Cleanup server
 */
void dare_ib_clt_shutdown()
{
    free_ib_device();
}

static void 
free_ib_device()
{        
    if (NULL != IBDEV) {
        ud_shutdown();
    
        if (NULL != IBDEV->ib_dev_context) {
            ibv_close_device(IBDEV->ib_dev_context);
        }
    
        free(IBDEV);
        IBDEV = NULL;
    }
}

#endif 

/* ================================================================== */
/* Starting a server */
#if 1

/**
 * Poll UD CQ and retrieve the first message
 */
uint8_t dare_ib_poll_ud_queue()
{
    return ud_get_message();
}

int dare_ib_join_cluster()
{
    return ud_join_cluster();
}

int dare_ib_exchange_rc_info()
{
    return ud_exchange_rc_info();
}

int dare_ib_update_rc_info()
{
    return ud_update_rc_info();
}

int dare_ib_get_replicated_vote()
{
    return rc_get_replicated_vote();
}

int dare_ib_send_sm_request()
{
    return rc_send_sm_request();
}

int dare_ib_send_sm_reply( uint8_t idx, void *s, int reg_mem )
{
    return rc_send_sm_reply(idx, s, reg_mem);
}

int dare_ib_recover_sm( uint8_t idx )
{
    return rc_recover_sm(idx);
}

int dare_ib_recover_log()
{
    return rc_recover_log();
}

#endif 

/* ================================================================== */
/* HB mechanism */
#if 1

/**
 * Send heartbeat to all remote servers
 * Note: need to reset the active array in case it was modified
 * by a different send operation
 */
int dare_ib_send_hb()
{
    return rc_send_hb();
}

int dare_ib_send_hb_reply( uint8_t idx )
{
    return rc_send_hb_reply(idx);
}

#endif 

/* ================================================================== */
/* Leader election */
#if 1

/**
 * Send vote requests to the other servers; that is,
 * write SID, index & term of last log entry
 */
int  dare_ib_send_vote_request()
{
    return rc_send_vote_request();
}

/**
 * Replicate SID of a the candidate that receives my vote;
 * future SIDs of this index cannot be lower than the replicated SID
 */
int dare_ib_replicate_vote()
{
    return rc_replicate_vote();
}

/**
 * Send ACK to the candidate
 */
int dare_ib_send_vote_ack()
{
    return rc_send_vote_ack();
}

#endif

/* ================================================================== */
/* Normal operation */
#if 1

int dare_ib_establish_leadership()
{
    return 0;//rc_establish_leadership();
}

/**
 * Write remote logs
 */
int dare_ib_write_remote_logs( int wait_for_commit )
{
    return rc_write_remote_logs(wait_for_commit);
}

/**
 * Get remote apply offsets
 */
int dare_ib_get_remote_apply_offsets()
{
    return rc_get_remote_apply_offsets();
}

#endif 

/* ================================================================== */
/* Handle client requests */
#if 1

int dare_ib_apply_cmd_locally()
{
    return ud_apply_cmd_locally();
}

int dare_ib_create_clt_request()
{
    return ud_create_clt_request();
}

int dare_ib_create_clt_downsize_request()
{
    return ud_create_clt_downsize_request();
}

int dare_ib_resend_clt_request()
{
    return ud_resend_clt_request();
}

int dare_ib_send_clt_reply( uint16_t lid, uint64_t req_id, uint8_t type )
{
    return ud_send_clt_reply(lid, req_id, type);
}

#endif 

/* ================================================================== */
/* Handle QPs state */
#if 1

void dare_ib_disconnect_server( uint8_t idx )
{
    int rc;
    rc = rc_disconnect_server(idx);
    if (0 != rc) {
        error(log_fp, "Cannot disconnect server %"PRIu8"\n", idx);
    }
}

/**
 * Revoke remote log access; that is, 
 * move the log QP to RESET state
 */
int dare_ib_revoke_log_access()
{
    return rc_revoke_log_access();
}

/**
 * Restore remote log access
 */
int dare_ib_restore_log_access()
{
    return rc_restore_log_access();
}

#endif 

/* ================================================================== */
/* Debugging */
#if 1

void print_rc_info()
{
    uint8_t i, size = get_extended_group_size(SRV_DATA->config);
    
    info(log_fp, "\n### RC info ###\n");
    for (i = 0; i < size; i++) {
        if (i == SRV_DATA->config.idx) {
            info(log_fp, "[%02"PRIu8"] ME\n", i);
            continue;
        }
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
            info(log_fp, "[%02"PRIu8"] OFF\n", i);
            continue;
        }
        dare_ib_ep_t *ep = 
            (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (0 == ep->rc_connected) { 
            info(log_fp, "[%02"PRIu8"] NO RC\n", i);
            continue;
        }
        info(log_fp, "[%02"PRIu8"]  log: "
                         "RQPN=%"PRIu32"; "
                         "LQPN=%"PRIu32"; "
                         "RMR=[%"PRIu64"; %"PRIu32"]; "
                         "LMR=[%"PRIu64"; %"PRIu32"]\n",
                          i, 
                          ep->rc_ep.rc_qp[LOG_QP].qpn, 
                          ep->rc_ep.rc_qp[LOG_QP].qp->qp_num,
                          ep->rc_ep.rmt_mr[LOG_QP].raddr, 
                          ep->rc_ep.rmt_mr[LOG_QP].rkey,
                          (uint64_t)IBDEV->lcl_mr[LOG_QP]->addr, 
                          IBDEV->lcl_mr[LOG_QP]->rkey);
        info(log_fp, "     ctrl: "
                         "RQPN=%"PRIu32"; "
                         "LQPN=%"PRIu32"; "
                         "RMR=[%"PRIu64"; %"PRIu32"]; "
                         "LMR=[%"PRIu64"; %"PRIu32"]\n",
                          ep->rc_ep.rc_qp[CTRL_QP].qpn, 
                          ep->rc_ep.rc_qp[CTRL_QP].qp->qp_num,
                          ep->rc_ep.rmt_mr[CTRL_QP].raddr, 
                          ep->rc_ep.rmt_mr[CTRL_QP].rkey,
                          (uint64_t)IBDEV->lcl_mr[CTRL_QP]->addr, 
                          IBDEV->lcl_mr[CTRL_QP]->rkey);
    }
    info(log_fp, "\n");
}

int print_qp_state( void *ep )
{
    return rc_print_qp_state(ep);
}

int dare_ib_print_ud_qp()
{
    int rc;
    struct ibv_qp_attr attr;
    struct ibv_qp_init_attr init_attr;
    rc = ibv_query_qp(IBDEV->ud_qp, &attr, IBV_QP_STATE, &init_attr);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_query_qp failed because %s\n",
                             strerror(rc));
    }
    info(log_fp, "UD QP state: %s\n", qp_state_to_str(attr.qp_state));
    return 0;
}

#endif

/* ================================================================== */
/* LogGP */
#if 1

double dare_ib_get_loggp_params( uint32_t size, int type, int *poll_count, int write, int inline_flag )
{
    return rc_get_loggp_params(size, type, poll_count, write, inline_flag);
}

double dare_ib_loggp_prtt( int n, double delay, uint32_t size, int inline_flag )
{
    //return rc_loggp_prtt(n, delay, size);
    return ud_loggp_prtt(n, delay, size, inline_flag);
}

int dare_ib_loggp_exit()
{
    return rc_loggp_exit();
}

#endif

/* ================================================================== */
/* Others */
#if 1 

static int
test_qp_support_rc( struct ibv_context *device_context )
{
    int rc;
    struct ibv_pd *pd = NULL;
    struct ibv_cq *cq = NULL; 
    struct ibv_qp_init_attr qpia;
    struct ibv_qp *qp = NULL;
    
    /* Try to make both the PD and CQ */
    pd = ibv_alloc_pd(device_context);
    if (NULL == pd) {
        return 1;
    }
    
    cq = ibv_create_cq(device_context, 2, NULL, NULL, 0);
    if (NULL == cq) {
        rc = 1;
        goto out;
    }

    /* Create QP of type IBV_QPT_RC */
    memset(&qpia, 0, sizeof(qpia));
    qpia.qp_context = NULL;
    qpia.send_cq = cq;
    qpia.recv_cq = cq;
    qpia.srq = NULL;
    qpia.cap.max_send_wr = 1;
    qpia.cap.max_recv_wr = 1;
    qpia.cap.max_send_sge = 1;
    qpia.cap.max_recv_sge = 1;
    qpia.cap.max_inline_data = 0;
    qpia.qp_type = IBV_QPT_RC;
    qpia.sq_sig_all = 0;
    
    qp = ibv_create_qp(pd, &qpia);
    if (NULL == qp) {
        rc = 1;
        goto out;
    }
    
    ibv_destroy_qp(qp);
    rc = 0;
 
out:
    /* Free the PD and/or CQ */
    if (NULL != pd) {
        ibv_dealloc_pd(pd);
    }
    if (NULL != cq) {
        ibv_destroy_cq(cq);
    }

    return rc;   
}

/** 
 * source: OpenMPI source
   Horrible.  :-( Per the thread starting here:
   http://lists.openfabrics.org/pipermail/general/2008-June/051822.html,
   we can't rely on the value reported by the device to determine the
   maximum max_inline_data value.  So we have to search by looping
   over max_inline_data values and trying to make dummy QPs.  Yuck! 
 */
int find_max_inline( struct ibv_context *context, 
                     struct ibv_pd *pd,
                     uint32_t *max_inline_arg )
{
    int rc;
    struct ibv_qp *qp = NULL;
    struct ibv_cq *cq = NULL;
    struct ibv_qp_init_attr init_attr;
    uint32_t max_inline_data;
    
    *max_inline_arg = 0;

    /* Make a dummy CQ */
    cq = ibv_create_cq(context, 1, NULL, NULL, 0);
    if (NULL == cq) {
        return 1;
    }
    
    /* Setup the QP attributes */
    memset(&init_attr, 0, sizeof(init_attr));
    init_attr.qp_type = IBV_QPT_RC;
    init_attr.send_cq = cq;
    init_attr.recv_cq = cq;
    init_attr.srq = 0;
    init_attr.cap.max_send_sge = 1;
    init_attr.cap.max_recv_sge = 1;
    init_attr.cap.max_recv_wr = 1;
    
    /* Loop over max_inline_data values; just check powers of 2 --
       that's good enough */
    init_attr.cap.max_inline_data = max_inline_data = 1 << 20;
    rc = 1;
    while (max_inline_data > 0) {
        qp = ibv_create_qp(pd, &init_attr);
        if (NULL != qp) {
            *max_inline_arg = max_inline_data;
            ibv_destroy_qp(qp);
            rc = 0;
            break;
        }
        max_inline_data >>= 1;
        init_attr.cap.max_inline_data = max_inline_data;
    }
    
    /* Destroy the temp CQ */
    if (NULL != cq) {
        ibv_destroy_cq(cq);
    }

    return rc;
}

void dare_ib_send_msg() 
{
    rc_ib_send_msg();
}

#endif 
