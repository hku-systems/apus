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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include<arpa/inet.h>

#include <dare_ibv_ud.h>
#include <dare_ibv_rc.h>
#include <dare_ibv.h>
#include <dare_ep_db.h>
#include <dare_server.h>
#include <dare_client.h>
#include <dare_kvs_sm.h>
#include <timer.h>
extern FILE *log_fp;

/* InfiniBand device */
extern dare_ib_device_t *dare_ib_device;

/* global_mgid is an IPV6 multicast address */
char* global_mgid;

#define IBDEV dare_ib_device
#define SRV_DATA ((dare_server_data_t*)dare_ib_device->udata)
#define CLT_DATA ((dare_client_data_t*)dare_ib_device->udata)

struct ibv_wc *wc_array;

/* ================================================================== */

static int 
ud_prerequisite( uint32_t receive_count );
static int
ud_memory_reg();
static void 
ud_memory_dereg();
static int 
ipv6_string_to_raw(char* ipv6_addr, uint8_t raw[16]);
static int 
ud_qp_create();
static void 
ud_qp_destroy();
static int
ud_post_receives();
static int
ud_post_one_receive( int idx );
static int 
ud_send_message( ud_ep_t *ud_ep, uint32_t length );
static uint8_t
handle_csm_read_requests( struct ibv_wc *read_wcs, uint16_t read_count );
static void 
handle_one_csm_read_request(struct ibv_wc *wc, client_req_t *request);
static uint8_t
handle_csm_write_requests( struct ibv_wc *read_wcs, uint16_t write_count );
static void 
handle_one_csm_write_request( struct ibv_wc *wc, client_req_t *request );
static uint8_t
handle_message_from_client( struct ibv_wc *wc, ud_hdr_t *ud_hdr );
static uint8_t
handle_message_from_server( struct ibv_wc *wc, ud_hdr_t *ud_hdr );
static int
handle_server_join_request(struct ibv_wc *wc, ud_hdr_t *request);
static int 
handle_rc_syn(struct ibv_wc *wc, rc_syn_t *msg);
static int 
handle_rc_synack(struct ibv_wc *wc, rc_syn_t *msg);
static int 
handle_rc_ack(struct ibv_wc *wc, rc_ack_t *msg);
static void
handle_downsize_request(struct ibv_wc *wc, reconf_req_t *request);
static int 
send_clt_request( uint32_t len );
static int 
handle_csm_reply(struct ibv_wc *wc, client_rep_t *request);
static void 
handle_server_join_reply(struct ibv_wc *wc, reconf_rep_t *reply);

static int
wc_to_ud_ep(ud_ep_t *ud_ep, struct ibv_wc *wc);
static int 
cmpfunc_uint64( const void *a, const void *b );

static int
mcast_ah_create();
//static void
//mcast_ah_destroy();
static int 
mcast_send_message( uint32_t len );

/* ================================================================== */
/* Init and cleaning up */
#if 1

int ud_init( uint32_t receive_count )
{
    int rc;

    rc = ud_prerequisite(receive_count);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot create UD prerequisite\n");
    }
       
    /* Register memory */
    rc = ud_memory_reg();
    if (0 != rc) {
        error_return(1, log_fp, "Cannot register memory\n");
    }
       
    /* Create QP to listen on client requests */
    rc = ud_qp_create();
    if (0 != rc) {
        error_return(1, log_fp, "Cannot create listen QP\n");
    }
    
    /* Create mcast Address Handle (AH) */
    rc = mcast_ah_create();
    if (0 != rc) {
        error_return(1, log_fp, "Cannot create AH\n");
    }
    
    /* Allocate memory for prefetching  UD requests */
    wc_array = (struct ibv_wc*)malloc(receive_count * sizeof(struct ibv_wc));

    return 0;
}

void ud_shutdown()
{
    if (NULL != wc_array) {
        free(wc_array);
    }
    if (NULL != IBDEV->ib_mcast_ah) {
        ibv_destroy_ah(IBDEV->ib_mcast_ah);
    }
    ud_qp_destroy();
    ud_memory_dereg(IBDEV->ud_rcqe);
    if (NULL != IBDEV->ud_scq) {
        ibv_destroy_cq(IBDEV->ud_scq);
    }
    if (NULL != IBDEV->ud_rcq) {
        ibv_destroy_cq(IBDEV->ud_rcq);
    }
    if (NULL != IBDEV->ud_pd) {
        ibv_dealloc_pd(IBDEV->ud_pd);
    }
        
}

struct ibv_ah* ud_ah_create( uint16_t dlid )
{
    struct ibv_ah *ah = NULL;
    struct ibv_ah_attr ah_attr;
     
    memset(&ah_attr, 0, sizeof(ah_attr));
     
    ah_attr.is_global     = 0;
    ah_attr.dlid          = dlid;
    ah_attr.sl            = 0;
    ah_attr.src_path_bits = 0;
    ah_attr.port_num      = IBDEV->port_num;

    ah = ibv_create_ah(IBDEV->ud_pd, &ah_attr);
    if (NULL == ah) {
        error(log_fp, "ibv_create_ah() failed because %s\n", strerror(errno));
        return NULL;
    }
    
    return ah;
}

static int
mcast_ah_create()
{
    struct ibv_ah *ah = NULL;
    struct ibv_ah_attr ah_attr;
     
    memset(&ah_attr, 0, sizeof(ah_attr));
    // ah_attr.dlid: If destination is in same subnet, the LID of the 
    // port to which the subnet delivers the packets to. 
    ah_attr.dlid          = IBDEV->mlid; // multicast
    ah_attr.sl            = 0;
    ah_attr.src_path_bits = 0;
    ah_attr.port_num      = IBDEV->port_num;
    ah_attr.is_global     = 1;  

    memset(&ah_attr.grh, 0, sizeof(struct ibv_global_route));
    memcpy(&(ah_attr.grh.dgid.raw), 
           &(IBDEV->mgid.raw), 
           sizeof(ah_attr.grh.dgid.raw));
    
    ah = ibv_create_ah(IBDEV->ud_pd, &ah_attr);
    if (NULL == ah) {
        error_return(1, log_fp, "ibv_create_ah() failed because %s\n", 
                     strerror(errno));
    }
    
    IBDEV->ib_mcast_ah = ah;

    return 0;
}

void ud_ah_destroy( struct ibv_ah* ah )
{
    int rc;
    
    if (NULL == ah) {
        return;
    }
        
    rc = ibv_destroy_ah(ah);
    if (0 != rc) {
        debug(log_fp, "ibv_destroy_ah() failed because %s\n", strerror(rc));
    }
    
    ah = NULL;
    
    return;
}

static int 
ud_prerequisite( uint32_t receive_count )
{
    /* Allocate the UD protection domain */
    IBDEV->ud_pd = ibv_alloc_pd(IBDEV->ib_dev_context);
    if (NULL == IBDEV->ud_pd) {
        error_return(1, log_fp, "Cannot allocate UD PD\n");
    }
    
    /* Create UD completion queues */
    IBDEV->ud_rcqe = receive_count;
    IBDEV->ud_rcq = ibv_create_cq(IBDEV->ib_dev_context, 
                   IBDEV->ud_rcqe, NULL, NULL, 0);
    if (NULL == IBDEV->ud_rcq) {
        error_return(1, log_fp, "Cannot create UD Receive CQ\n");
    }
    IBDEV->ud_scq = ibv_create_cq(IBDEV->ib_dev_context, 
                                   IBDEV->ud_rcqe, NULL, NULL, 0);
    if (NULL == IBDEV->ud_scq) {
        error_return(1, log_fp, "Cannot create UD Send CQ\n");
    }

    /* Find max inlinre */
    if (0 != find_max_inline(IBDEV->ib_dev_context,
                             IBDEV->ud_pd,
                             &IBDEV->ud_max_inline_data)) 
    {
        error_return(1, log_fp, "Cannot find max UD inline data\n");
    }
    return 0;
}

static int
ud_memory_reg()
{
    int i;
    
    /* Register memory for receive buffers - listen to requests */
    IBDEV->ud_recv_bufs = (void**)
            malloc(IBDEV->ud_rcqe * sizeof(void*));
    IBDEV->ud_recv_mrs = (struct ibv_mr**)
            malloc(IBDEV->ud_rcqe * sizeof(struct ibv_mr*));
    if ( (NULL == IBDEV->ud_recv_bufs) || 
         (NULL == IBDEV->ud_recv_mrs) ) {
        error_return(1, log_fp, "Cannot allocate memory for receive buffers");
    }
    for (i = 0; i < IBDEV->ud_rcqe; i++) {
        /* Allocate buffer: cannot be larger than MTU */
        //posix_memalign(&IBDEV->ud_recv_bufs[i], 8, 
        //                mtu_value(IBDEV->mtu));
        
        IBDEV->ud_recv_bufs[i] = malloc(mtu_value(IBDEV->mtu));
        if (NULL == IBDEV->ud_recv_bufs[i]) {
            error_return(1, log_fp, "Cannot allocate memory for receive buffers");
        }
        memset(IBDEV->ud_recv_bufs[i], 0, mtu_value(IBDEV->mtu));
        IBDEV->ud_recv_mrs[i] = ibv_reg_mr(
            IBDEV->ud_pd, 
            IBDEV->ud_recv_bufs[i], 
            mtu_value(IBDEV->mtu), 
            IBV_ACCESS_LOCAL_WRITE);
        if (NULL == IBDEV->ud_recv_mrs[i]) {
            error_return(1, log_fp, "Cannot register memory for receive buffers");
        }
    }
    
    /* Register memory for send buffer - reply to requests */
    IBDEV->ud_send_buf = malloc(mtu_value(IBDEV->mtu));
    if (NULL == IBDEV->ud_send_buf) {
        error_return(1, log_fp, "Cannot allocate memory for send buffer");
    }
    memset(IBDEV->ud_send_buf, 0, mtu_value(IBDEV->mtu)); 
    IBDEV->ud_send_mr = ibv_reg_mr(
        IBDEV->ud_pd, 
        IBDEV->ud_send_buf, 
        mtu_value(IBDEV->mtu), 
        IBV_ACCESS_LOCAL_WRITE);
    if (NULL == IBDEV->ud_send_mr) {
        error_return(1, log_fp, "Cannot register memory for send buffer");
    }
    
    return 0;
}

static void 
ud_memory_dereg()
{
    int i;
    int rc;
    
    /* Deregister memory for receiver buffers */
    if (NULL != IBDEV->ud_recv_mrs) {
        for (i = 0; i < IBDEV->ud_rcqe; i++) {
            if (NULL == IBDEV->ud_recv_mrs[i])
                continue;
            rc = ibv_dereg_mr(IBDEV->ud_recv_mrs[i]);
            if (0 != rc) {
                error(log_fp, "Cannot deregister memory");
            }
        }
        free(IBDEV->ud_recv_mrs);
        IBDEV->ud_recv_mrs = NULL;
    }
    /* Free memory for receiver buffers */
    if (NULL != IBDEV->ud_recv_bufs) {
        for (i = 0; i < IBDEV->ud_rcqe; i++) {
            if (NULL == IBDEV->ud_recv_bufs[i])
                continue;
            free(IBDEV->ud_recv_bufs[i]);
        }
        free(IBDEV->ud_recv_bufs);
        IBDEV->ud_recv_bufs = NULL;
    }
    
    /* Deregister memory for send buffer */
    if (NULL != IBDEV->ud_send_mr) {
        rc = ibv_dereg_mr(IBDEV->ud_send_mr);
        if (0 != rc) {
            error(log_fp, "Cannot deregister memory");
        }
    }
    if (NULL != IBDEV->ud_send_buf) {
        free(IBDEV->ud_send_buf);
        IBDEV->ud_send_buf = NULL;
    }
}

/**
 * Convert an IPv6 address string to its equivalent binary format -- 
 * an array of 16 elements of type uint8_t
 */
static int 
ipv6_string_to_raw(char* ipv6_addr, uint8_t raw[16])
{
    struct in6_addr result;
    info(log_fp, "ipv6_addr: [%s]\n", ipv6_addr);

    // Check that a valid IPV6 address has been provide
    if (!inet_pton(AF_INET6,ipv6_addr,&result)) {
        error_return(1, log_fp, "inet_pton");
    }

    // write the result to passed destination array of uint8_t type
    int i;
    for(i=0; i<16; i++)
    raw[i]=(uint8_t)(result.s6_addr[i]);

    //Uncomment the following code to debug without using debugger
    //printf("\nThe raw address is\n");
    //for(i=0;i<16;i++)
    //printf("0x%02x ",raw[i]); 
    //printf("\n");

    return 0;//success
}


static int 
ud_qp_create()
{
    int rc;
    struct ibv_qp_init_attr init_attr;
    struct ibv_qp_attr attr;
    struct ibv_qp *qp = NULL;

    /* create the UD keypair */
    memset(&init_attr, 0, sizeof(init_attr));
    init_attr.qp_type = IBV_QPT_UD;
    init_attr.send_cq = IBDEV->ud_scq;
    init_attr.recv_cq = IBDEV->ud_rcq;
    init_attr.cap.max_inline_data = IBDEV->ud_max_inline_data;
    init_attr.cap.max_send_sge    = 1;
    init_attr.cap.max_recv_sge    = 1;
    init_attr.cap.max_recv_wr = IBDEV->ud_rcqe;
    init_attr.cap.max_send_wr = IBDEV->ud_rcqe;

    qp = ibv_create_qp(IBDEV->ud_pd, &init_attr); 
    if (NULL == qp) {
        error_return(1, log_fp, "Could not create UD listen queue pair");
    }
    /* end: create the UD queue pair */
    
    /* Attach the QP to a multicast group */
    // saquery -g
    // Usage: saquery [options] [query-name] [<name> | <lid> | <guid>]
    // Options:
    //   -g get multicast group info   
    
    uint8_t raw[16]; 
    if (!global_mgid) {
        error_return(1, log_fp, "mcast address is NULL\n");
    }
    if (ipv6_string_to_raw(global_mgid, raw)) {
        error_return(1, log_fp, "ipv6_string_to_raw");
    }
    info(log_fp, "# mcast addr: [%s]\n", global_mgid);

    //Uncomment the following code to see the contents of raw
    // printf("\nThe GLOBAL raw address is\n");
    // int i;
    // for(i=0;i<16;i++)
    // printf("0x%02x ",raw[i]);
    // printf("\n");
    
    //printf("\n The Global MGID is set to : % \n", raw);
   
    memcpy(&(IBDEV->mgid.raw), &raw, sizeof(raw));
    // castor: 0xC003
    // euler: 0xC001
    IBDEV->mlid = 0xc001;
    //IBDEV->mlid = 0xc003;
    rc = ibv_attach_mcast(qp, 
                          &IBDEV->mgid, 
                          IBDEV->mlid);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_attach_mcast() failed because %s\n", strerror(rc));
    }

    /* move the UD QP into the INIT state */
    memset(&attr, 0, sizeof(attr));
    attr.qp_state   = IBV_QPS_INIT;
    attr.pkey_index = IBDEV->pkey_index;
    attr.port_num   = IBDEV->port_num;
    attr.qkey       = 0;

#if DEBUG    
    info(log_fp, "# pkey index = %"PRIu16" \n", IBDEV->pkey_index);
    uint16_t pkey;
    ibv_query_pkey(IBDEV->ib_dev_context, 
                   IBDEV->port_num,
                   IBDEV->pkey_index,
                   &pkey);
    info(log_fp, "# pkey = %"PRIu16" \n", pkey);
#endif 

    rc = ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX
                       | IBV_QP_PORT | IBV_QP_QKEY); 
    if (0 != rc) {
        error_return(1, log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
    }

    /* Move listen QP to RTR */
    attr.qp_state = IBV_QPS_RTR;

    rc = ibv_modify_qp(qp, &attr, IBV_QP_STATE); 
    if (0 != rc) {
        error_return(1, log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
    }

    /* Move listen QP to RTS */
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    
    /* Set the psn to a random value */
    // srand48(getpid() * time(NULL));
    //attr.sq_psn = lrand48() & 0xffffff;
    attr.sq_psn = 0;
    
    rc = ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN); 
    if (0 != rc) {
        error_return(1, log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
    }
        
    
    IBDEV->ud_qp = qp;

    return 0;
}

static void 
ud_qp_destroy()
{
    int rc;
    struct ibv_qp_attr attr;
    struct ibv_wc wc;

    if (NULL == IBDEV->ud_qp) {
        return;
    }

    do {
        /* Move listen QP into the ERR state to cancel all outstanding
           work requests */
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_ERR;
        attr.sq_psn = 0;
        
        debug(log_fp, "Setting UD QP to err state\n");

        rc = ibv_modify_qp(IBDEV->ud_qp, &attr, IBV_QP_STATE);
        if (0 != rc) {
            debug(log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
            break;
        }

        while (ibv_poll_cq(IBDEV->ud_rcq, 1, &wc) > 0);

        /* move the QP into the RESET state */
        memset(&attr, 0, sizeof(attr));
        attr.qp_state = IBV_QPS_RESET;
        
        debug(log_fp, "Setting UD QP to reset state\n");

        rc = ibv_modify_qp(IBDEV->ud_qp, &attr, IBV_QP_STATE);
        if (0 != rc) {
            debug(log_fp, "ibv_modify_qp failed because %s\n", strerror(rc));
            break;
        }
    } while (0);
    
    rc = ibv_detach_mcast(IBDEV->ud_qp, 
                          &IBDEV->mgid, 
                          IBDEV->mlid);
    if (0 != rc) {
        debug(log_fp, "ibv_detach_mcast() failed because %s\n", strerror(rc));
    }

    rc = ibv_destroy_qp(IBDEV->ud_qp);
    if (0 != rc) {
        debug(log_fp, "ibv_destroy_qp failed because %s\n", strerror(rc));
    }

    IBDEV->ud_qp = NULL;
}

#endif 

/* ================================================================== */
/* Starting up UD connection */
#if 1

/**
 * Post UD receives; request CQ notification and start UD listener
 * Note: After this call, we may receive messages over UD
 */
int ud_start()
{
    int rc;
    
    /* Post receives */
    rc = ud_post_receives();
    if (0 != rc) {
        error_return(1, log_fp, "Cannot post receives\n");
    }

    return 0;
}

static int
ud_post_receives()
{
    int i;
    int rc;
    struct ibv_recv_wr *bad_wr = NULL;
    struct ibv_recv_wr wr_array[MAX_CLIENT_COUNT];
    struct ibv_sge sg_array[MAX_CLIENT_COUNT];
    
    for (i = 0; i < IBDEV->ud_rcqe; i++) {
        memset(&sg_array[i], 0, sizeof(struct ibv_sge));
        sg_array[i].addr   = (uint64_t)(IBDEV->ud_recv_bufs[i]);
        sg_array[i].length = mtu_value(IBDEV->mtu);
        sg_array[i].lkey   = IBDEV->ud_recv_mrs[i]->lkey;
        
        memset(&wr_array[i], 0, sizeof(struct ibv_recv_wr));
        wr_array[i].wr_id   = i;
        wr_array[i].sg_list = &sg_array[i];
        wr_array[i].num_sge = 1;
        wr_array[i].next    = (i == IBDEV->ud_rcqe-1) ? NULL : 
                            &wr_array[i+1];
    }
    
    rc = ibv_post_recv(IBDEV->ud_qp, wr_array, &bad_wr);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_post_recv failed because %s\n", strerror(rc));
    }
    return 0;
}

static int
ud_post_one_receive( int idx )
{
    int rc;
    struct ibv_recv_wr *bad_wr = NULL;
    struct ibv_recv_wr wr;
    struct ibv_sge sg;
    
    memset(&sg, 0, sizeof(struct ibv_sge));
    sg.addr   = (uint64_t)IBDEV->ud_recv_bufs[idx];
    sg.length = mtu_value(IBDEV->mtu);
    sg.lkey   = IBDEV->ud_recv_mrs[idx]->lkey;
        
    memset(&wr, 0, sizeof(struct ibv_recv_wr));
    wr.wr_id   = idx;
    wr.sg_list = &sg;
    wr.num_sge = 1;
    
    rc = ibv_post_recv(IBDEV->ud_qp, &wr, &bad_wr);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_post_recv failed because %s\n", strerror(rc));
    }
    return 0;
}

#endif

/* ================================================================== */
/* Handle UD messages */
#if 1

int loggp_not_inline = 0;
static int 
ud_send_message( ud_ep_t *ud_ep, uint32_t len )
{
    int rc;
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr = NULL;

    //debug(log_fp, "Sending UD message to server with LID=%"PRIu16"\n", ud_ep->lid);
    if (len > mtu_value(IBDEV->mtu)) {
        debug(log_fp, "Cannot send more than %"PRIu32" bytes\n", 
              mtu_value(IBDEV->mtu));
        len = mtu_value(IBDEV->mtu);
    }

    //dump_bytes(log_fp, IBDEV->ud_send_buf, len, "sent bytes");

    memset(&sg, 0, sizeof(sg));
    sg.addr   = (uint64_t)IBDEV->ud_send_buf;
    sg.length = len;
    sg.lkey   = IBDEV->ud_send_mr->lkey;
     
    memset(&wr, 0, sizeof(wr));
    wr.wr_id      = 0;
    wr.sg_list    = &sg;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    if ( (len <= IBDEV->ud_max_inline_data) && !loggp_not_inline ) {
        wr.send_flags |= IBV_SEND_INLINE;
    }
    wr.wr.ud.ah          = ud_ep->ah;
    // send_wr.wr.ud.remote_qpn(X) must be equal to qp->qp_num(Y)
    wr.wr.ud.remote_qpn  = ud_ep->qpn;
    wr.wr.ud.remote_qkey = 0;
     
    rc = ibv_post_send(IBDEV->ud_qp, &wr, &bad_wr);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_post_send failed because %s\n", strerror(rc));
    }

    /* Wait for send operation to complete */
    struct ibv_wc wc;
    int num_comp;
     
    do {
        num_comp = ibv_poll_cq(IBDEV->ud_scq, 1, &wc);
    } while (num_comp == 0);
      
    if (num_comp < 0) {
        error_return(1, log_fp, "ibv_poll_cq() failed\n");
    }
       
    /* Verify the completion status */
    if (wc.status != IBV_WC_SUCCESS) {
    error_return(1, log_fp, "Failed status %s (%d) for wr_id %d\n", 
                 ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
    }

    return 0;
}

/**
Connecting UD QPs

Assuming that we connect UD QP in node X and UD QP in node Y and each side creates an AH using ah_attr.

    * In side X the P_Key value at the port’s qp_attr.port_num(X) P_Key 
      table[qp_attr.pkey_index(X)] must be equal to same at side Y 
      (what is matters is the P_Key value and not its index in the table) 
      and at least one of them must be full member [pkey_index = 0]
    * qp_attr.port_num(X) must be equal to ah_attr.port_num(X) [done]
        * If using unicast: the LID in qp_attr.ah_attr.dlid(X) must be 
          assigned to port qp_attr.port_num(Y) in side Y
        * If using multicast: QP(Y) must be a member of the multicast 
          group of the LID qp_attr.ah_attr.dlid(X)
    * send_wr.wr.ud.remote_qpn(X) must be equal to qp->qp_num(Y)
    * qp_attr.qkey(X) should be equal to qp_attr.qkey(Y) unless a different 
      Q_Key value is used in send_wr.wr.ud.remote_qkey(X) when sending a message
      [qkey = 0]
*/
static int 
mcast_send_message( uint32_t len )
{
    int rc;
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr = NULL;
    
    text(log_fp, "## Sending mcast message (len=%"PRIu32")\n", len);
    if (len > mtu_value(IBDEV->mtu)) {
        debug(log_fp, "Length = %"PRIu32"; cannot send more than %"PRIu32" bytes\n", 
              len, mtu_value(IBDEV->mtu));
        len = mtu_value(IBDEV->mtu);
    }

    //dump_bytes(log_fp, IBDEV->ud_send_buf, len, "mcast bytes");
     
    memset(&sg, 0, sizeof(sg));
    sg.addr   = (uint64_t)IBDEV->ud_send_buf;
    sg.length = len;
    sg.lkey   = IBDEV->ud_send_mr->lkey;
     
    memset(&wr, 0, sizeof(wr));
    wr.wr_id      = 0;
    wr.sg_list    = &sg;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    if (len <= IBDEV->ud_max_inline_data) {
        wr.send_flags |= IBV_SEND_INLINE;
    }
    wr.wr.ud.ah          = IBDEV->ib_mcast_ah;
    wr.wr.ud.remote_qpn  = 0xFFFFFF; // multicast: 0xFFFFFF
    wr.wr.ud.remote_qkey = 0;
     
    rc = ibv_post_send(IBDEV->ud_qp, &wr, &bad_wr);
    if (0 != rc) {
        error_return(1, log_fp, "ibv_post_send failed because \"%s\"\n", 
                    strerror(rc));
    }

    /* Wait for send operation to complete */
    struct ibv_wc wc;
    int num_comp;
     
    do {
        num_comp = ibv_poll_cq(IBDEV->ud_scq, 1, &wc);
    } while (num_comp == 0);
      
    if (num_comp < 0) {
        error_return(1, log_fp, "ibv_poll_cq() failed\n");
    }
       
    /* Verify the completion status */
    if (wc.status != IBV_WC_SUCCESS) {
       error_return(1, log_fp, "Failed status %s (%d) for wr_id %d\n", 
                 ibv_wc_status_str(wc.status), wc.status, (int)wc.wr_id);
    }

    return 0;
}

uint8_t ud_get_message()
{
    int ne, i, j;
    uint8_t type = MSG_NONE, prev_type = MSG_NONE;
    struct ibv_wc *wc = wc_array;
    uint16_t wc_count = 0, rd_wr_count = 0;
    ud_hdr_t *ud_hdr;
    uint8_t read_flag = 0;

get_message:    
    ne = ibv_poll_cq(IBDEV->ud_rcq, 1, wc);
    if (ne < 0) {
        error_return(MSG_ERROR, log_fp, "Couldn't poll completion queue\n");
    }
    if (ne == 0) {
        goto handle_messages;
    }
    if (wc->status != IBV_WC_SUCCESS) {
        error_return(MSG_ERROR, log_fp, 
            "Completion with status 0x%x was found\n", wc->status);
    }
    if (wc->slid == IBDEV->lid) {
        /* Rearm and try again */
        ud_post_one_receive(wc->wr_id);
        goto get_message;
        //goto handle_messages;
    }
    if (wc->opcode & IBV_WC_RECV) {
        /* For UD: the number of bytes transferred is the 
           payload of the message plus the 40 bytes reserved 
           for the GRH */
        ud_hdr = (ud_hdr_t*)(IBDEV->ud_recv_bufs[wc->wr_id] + 40);
        //debug(log_fp, "byte_len = %"PRIu32"\n", wc->byte_len);
        //debug(log_fp, "type = %"PRIu8"\n", ud_hdr->type);
        //debug(log_fp, "ID = %"PRIu64"\n", ud_hdr->id);
        //dump_bytes(log_fp, ud_hdr, wc->byte_len - 40, "received bytes");
        /* Increase WC count */
        wc_count++; wc++;
        /* Only the server can receive READ or WRITE requests */
        if (IBV_SERVER != IBDEV->ulp_type) goto handle_messages;
        /* Check the type of the operation */
        type = ud_hdr->type;
        if (MSG_NONE == prev_type) {
            prev_type = type;
        }
        if ( (CSM_READ == type) && (CSM_READ == prev_type) ) {
            /* Read request; gather more */
            rd_wr_count++;
            read_flag = 1;
            goto get_message;
        }
        else //goto handle_messages;
        if ( (CSM_WRITE == type) && (CSM_WRITE == prev_type) ) {
            /* Write request; gather more */
            rd_wr_count++;
            //if (4 == rd_wr_count) goto handle_messages; // exceed MTU size
            goto get_message;
        }
    }
    else {
        /* Rearm and try again */
        ud_post_one_receive(wc->wr_id);
        goto get_message;
    }

handle_messages:    
    /* Handle read/write requests */
    if (rd_wr_count) {
        /* Simulate a server's CPU failure; the NIC & memory still works */
        //if (!is_leader()) {
        //    info_wtime(log_fp, "Received message over UD: type=%"PRIu8"\n", type);
        //    sleep(10);
        //    return type;
        //}

        if (read_flag) {
            /* Read requests */
            //info_wtime(log_fp, "Handle %"PRIu16" read requests\n", rd_wr_count);
            type = handle_csm_read_requests(wc_array, rd_wr_count);
        }
        else {
            /* Write requests */
            //info_wtime(log_fp, "Handle %"PRIu16" write requests\n", rd_wr_count);
            type = handle_csm_write_requests(wc_array, rd_wr_count);
        }
        /* Rearm */
        for (i = 0; i < rd_wr_count; i++) {
            ud_post_one_receive(wc_array[i].wr_id);
        }
    }
    /* Handle other messages */    
    if (wc_count > rd_wr_count) {
        /* There is one more message */
        ud_hdr = (ud_hdr_t*)
                (IBDEV->ud_recv_bufs[wc_array[wc_count-1].wr_id] + 40);
        if (IBV_SERVER == IBDEV->ulp_type) {
            type = handle_message_from_client(&wc_array[wc_count-1], ud_hdr);
        }
        else if (IBV_CLIENT == IBDEV->ulp_type) {
            type = handle_message_from_server(&wc_array[wc_count-1], ud_hdr);
        }
        /* Rearm receive operation */
        ud_post_one_receive(wc_array[wc_count-1].wr_id);
        return type;
    }    
    
    return type;
}

//#define WRITE_BENCH
#if defined(READ_BENCH) || defined(WRITE_BENCH)
uint64_t ticks[1000];
int measure_count = 0;
#endif 

/** 
 * Handle together a set of CSM READ requests
 */
static uint8_t
handle_csm_read_requests( struct ibv_wc *read_wcs, uint16_t read_count )
{
    int rc;
    uint16_t i;
    uint8_t type = MSG_ERROR;
    
    if (!is_leader()) {
        /* Ignore requests */
        return MSG_NONE;
    }
    //info_wtime(log_fp, "RECEIVED %"PRIu16" Read Requests\n", read_count);
                
    /* Server needs to verify if it's still the leader; 
    do it once for all read request -> higher throughput */
    int leader = 0;
#ifdef READ_BENCH    
    HRT_GET_TIMESTAMP(SRV_DATA->t2);
#endif    
    rc = rc_verify_leadership(&leader);
    if (0 != rc) {
        error_return(MSG_ERROR, log_fp, "Cannot verify leadership\n");
    }
    if (0 == leader) {
        /* I'm not the leader */
        return CSM_READ;
    }
    //HRT_GET_TIMESTAMP(SRV_DATA->t2);
    
    for (i = 0; i < read_count; i++) {
        handle_one_csm_read_request(&read_wcs[i], 
            (client_req_t*)(IBDEV->ud_recv_bufs[read_wcs[i].wr_id] + 40));
    }
    //HRT_GET_TIMESTAMP(SRV_DATA->t2);
#ifdef READ_BENCH    
    HRT_GET_ELAPSED_TICKS(SRV_DATA->t1, SRV_DATA->t2, &ticks[measure_count]);
    measure_count++;
    if (measure_count == 1000) {
        qsort(ticks, measure_count, sizeof(uint64_t), cmpfunc_uint64);
        info(log_fp, "Read request time: %lf (%lf, %lf)\n", 
            HRT_GET_USEC(ticks[measure_count/2]), HRT_GET_USEC(ticks[19]), 
            HRT_GET_USEC(ticks[measure_count-21]));
        info(log_fp, "Received %"PRIu32" bytes\n", read_wcs[0].byte_len - 40);
        measure_count = 0;
    }
    HRT_GET_TIMESTAMP(SRV_DATA->t1);
#endif
    return CSM_READ;
}

/** 
 * Handle a single CSM READ request
 */
static void 
handle_one_csm_read_request( struct ibv_wc *wc, client_req_t *request )
{
    int rc;
    
    /* Find the ep that send this request */
    dare_ep_t *ep = ep_search(&SRV_DATA->endpoints, wc->slid);
    if (ep == NULL) {
        /* No ep with this LID; create a new one */
        ep = ep_insert(&SRV_DATA->endpoints, wc->slid);
    }
    ep->ud_ep.qpn = wc->src_qp;
#if 1
    /* Check the status of the last write request  */
    if (ep->wait_for_idx != 0) {
        /* Read request already waiting for a write request */
        if (ep->wait_for_idx > SRV_DATA->last_cmt_write_csm_idx) {
            /* Write request not committed yet */
            return;
        }
    }
    else if (SRV_DATA->last_cmt_write_csm_idx < SRV_DATA->last_write_csm_idx) {
        /* There are not-committed write requests; so wait */
        ep->wait_for_idx = SRV_DATA->last_write_csm_idx;
        memcpy(ep->last_read_request, request, wc->byte_len - 40);
        return;
    }
#endif

    /* Create reply */
    client_rep_t *reply = (client_rep_t*)IBDEV->ud_send_buf;
    memset(reply, 0, sizeof(client_rep_t));
    reply->hdr.id = request->hdr.id;
    reply->hdr.type = CSM_REPLY;
    
    /* Get data from SM */
    rc = SRV_DATA->sm->apply_cmd(SRV_DATA->sm, &request->cmd, &reply->data);
    if (0 != rc) {
        error(log_fp, "Cannot apply read operation to the state machine\n");
    }
    
    /* Send reply */
    uint32_t len = sizeof(client_rep_t) + reply->data.len;
    rc = ud_send_message(&ep->ud_ep, len);
    if (0 != rc) {
        error(log_fp, "Cannot send message over UD to %"PRIu16"\n", 
                     ep->ud_ep.lid);
    }
#ifdef READ_BENCH    
    if (measure_count == 999) {
        info_wtime(log_fp, "Sent %"PRIu32" bytes\n", len);
    }
#endif
}

/** 
 * Handle together a set of CSM WRITE requests
 */
//#define HISTO_BATCHING
#ifdef HISTO_BATCHING
int hist[9];
int total_req;
#endif
static uint8_t
handle_csm_write_requests( struct ibv_wc *write_wcs, uint16_t write_count )
{
    int rc;
    uint16_t i;
    uint8_t type = MSG_ERROR;
    
    if (!is_leader()) {
        /* Ignore request */
        return MSG_NONE;
    }
#ifdef WRITE_BENCH   
    //HRT_GET_TIMESTAMP(SRV_DATA->t1);
#endif    
    //info_wtime(log_fp, "RECEIVED %"PRIu16" Write Requests\n", write_count);
    
    for (i = 0; i < write_count; i++) {
        handle_one_csm_write_request(&write_wcs[i], 
            (client_req_t*)(IBDEV->ud_recv_bufs[write_wcs[i].wr_id] + 40));
    }
#ifdef HISTO_BATCHING
    hist[write_count-1]++;
    total_req++;
#endif    
#ifdef WRITE_BENCH    
    if (measure_count == 999) {
        info(log_fp, "Received %"PRIu16" write reqs of %"PRIu32" bytes\n", write_count, write_wcs[0].byte_len - 40);
    }
#endif     
    
    return CSM_WRITE;
}

/** 
 * Handle a single CSM WRITE request
 */
static void 
handle_one_csm_write_request( struct ibv_wc *wc, client_req_t *request )
{
    int rc;

    /* TODO !!!! implement protocol SM that stores clients and their 
    most recent request; therefore, a client cannot issue 
    the same write operation twice (not implemented) */
    
#ifdef HISTO_BATCHING
    static int clt_count = 0;
#endif    
    
    /* Find the endpoint that send this request */
    dare_ep_t *ep = ep_search(&SRV_DATA->endpoints, wc->slid);
    if (ep ==  NULL) {
        /* No ep with this LID; create a new one */
        ep = ep_insert(&SRV_DATA->endpoints, wc->slid);
//info_wtime(log_fp, "New client\n");        
#ifdef HISTO_BATCHING
        /* #client++ -> print histogram info for previous number of clients
         * batching factor of requests */
        if (total_req != 0) {       
            clt_count ++;
            info(log_fp, "#Histo with %d clients (%d)\n", clt_count, total_req);
            int i;
            for (i = 0; i < 9; i++) {
                info(log_fp, "%d(%6.4lf\%)\n", hist[i], 100.*hist[i]/total_req);
                hist[i] = 0;
            }
            total_req = 0;
        }
#endif 
    }
    ep->ud_ep.qpn = wc->src_qp;
#ifdef HISTO_BATCHING
    if ( (clt_count == 8) && (total_req == 50000) ) {
        clt_count++;
        info(log_fp, "#Histo with %d clients (%d)\n", clt_count, total_req);
        int i;
        for (i = 0; i < 9; i++) {
            info(log_fp, "%d(%6.4lf\%)\n", hist[i], 100.*hist[i]/total_req);
            hist[i] = 0;
        }
        total_req = 0;
    }
#endif    

    if (ep->last_req_id >= request->hdr.id) {
        /* Already received this request */
        if (!ep->committed) {
            info(log_fp, "   # CMD not yet committed\n");
            print_rc_info();
            return;
        }
        rc = ud_send_clt_reply(wc->slid, request->hdr.id, CSM);
        if (0 != rc) {
            error(log_fp, "Cannot send client reply\n");
        }
        return;
    }
    ep->last_req_id = request->hdr.id;
    ep->committed = 0;
#ifdef WRITE_BENCH   
    uint64_t prev_end = SRV_DATA->log->end;
    //HRT_GET_TIMESTAMP(SRV_DATA->t1);
    //HRT_GET_TIMESTAMP(SRV_DATA->t2);
#endif    
    /* Append new entry */  
    SRV_DATA->last_write_csm_idx = log_append_entry(SRV_DATA->log, 
            SID_GET_TERM(SRV_DATA->ctrl_data->sid), request->hdr.id, 
            wc->slid, CSM, &request->cmd);
#ifdef WRITE_BENCH   
    if (measure_count == 999) {
        info(log_fp, "Adding %"PRIu64" bytes to the log\n", 
            log_offset_end_distance(SRV_DATA->log, prev_end));
    }
    //HRT_GET_TIMESTAMP(SRV_DATA->t1);
#endif
    //info_wtime(log_fp, "NEW LOG ENTRY (RSM OP) with IDX=%"PRIu64"\n", 
    //               SRV_DATA->last_write_csm_idx);
}

/**
 * Handle UD messages incoming from clients 
 */
static uint8_t
handle_message_from_client( struct ibv_wc *wc, ud_hdr_t *ud_hdr )
{
    int rc;
    uint8_t type = ud_hdr->type;
    switch(type) {
        case JOIN:
        {
            /* Join requests from server */
            if (!is_leader()) {
                /* Ignore request */
                break;
            }
            info(log_fp, ">> Received join request from server with lid%"
                PRIu16"\n", wc->slid);
            /* Handle reply */
            rc = handle_server_join_request(wc, ud_hdr);
            if (0 != rc) {
                error(log_fp, "The initiator cannot handle server join requests\n");
                type = MSG_ERROR;
            }
            break;
        }
        case RC_SYN:
        {
            /* First message of the 3-way handshake protocol */
            //info(log_fp, ">> Received RC_SYN from lid%"PRIu16"\n", wc->slid);
            type = MSG_NONE;
            rc = handle_rc_syn(wc, (rc_syn_t*)ud_hdr);
            if (0 != rc) {
                error(log_fp, "Cannot handle RC_SYN msg\n");
                type = MSG_ERROR;
            }
            break;
        }
        case RC_SYNACK:
        {
            /* Second message of the 3-way handshake protocol */
            //info(log_fp, ">> Received RC_SYNACK from lid%"PRIu16"\n", wc->slid);
            type = MSG_NONE;
            rc = handle_rc_synack(wc, (rc_syn_t*)ud_hdr);
            if (0 != rc) {
                if (REQ_MAJORITY == rc) {
                    type = ud_hdr->type;
                    break;
                }
                error(log_fp, "Cannot handle RC_SYNACK msg\n");
                type = MSG_ERROR;
            }
            break;
        }
        case RC_ACK:
        {
            /* Third message of the 3-way handshake protocol */
            //info(log_fp, ">> Received RC_ACK from lid%"PRIu16"\n", wc->slid);
            type = MSG_NONE;
            rc = handle_rc_ack(wc, (rc_ack_t*)ud_hdr);
            if (0 != rc) {
                if (REQ_MAJORITY == rc) {
                    type = ud_hdr->type;
                    break;
                }
                error(log_fp, "Cannot handle RC_ACK msg\n");
                type = MSG_ERROR;
            }
            break;
        }
        case CSM_READ:
        {
            /* Read request from a client */
            //info(log_fp, ">> Received read request from a client with lid%"
            //    PRIu16"\n", wc->slid);
            if (!is_leader()) {
                /* Ignore request */
                break;
            }
            text_wtime(log_fp, "CLIENT READ REQUEST %"PRIu64" (lid%"PRIu16")\n", 
                        ud_hdr->id, wc->slid);
            /* Handle request */
            handle_one_csm_read_request(wc, (client_req_t*)ud_hdr);
            break;
        }
        case CSM_WRITE:
        {
            /* Write request from a client */
            //info(log_fp, ">> Received write request from a client with lid%"
            //    PRIu16"\n", wc->slid);
            if (!is_leader()) {
                /* Ignore request */
                break;
            }
            text_wtime(log_fp, "CLIENT WRITE REQUEST %"PRIu64" (lid%"PRIu16")\n", 
                        ud_hdr->id, wc->slid);
            /* Handle request */
            handle_one_csm_write_request(wc, (client_req_t*)ud_hdr);
            break;
        }
        case DOWNSIZE:
        {
            /* Request for a group downsize */
            if (!is_leader()) {
                /* Ignore request */
                break;
            }
            info(log_fp, ">> Received group downsize request from a" 
                         " client with lid%"PRIu16"\n", wc->slid);
            /* Handle request */
            handle_downsize_request(wc, (reconf_req_t*)ud_hdr);
            break;
        }
        case CFG_REPLY:
        {
            /* PSM reply for a join request */
            info(log_fp, ">> Received CFG_REPLY from server with lid%"
                PRIu16"\n", wc->slid);
            handle_server_join_reply(wc, (reconf_rep_t*)ud_hdr);
            break;
        }
        default:
        {
            //debug(log_fp, "Unknown message\n");
        }
    }
    return type;
}

/**
 * Handle UD messages incoming from servers 
 */
static uint8_t
handle_message_from_server( struct ibv_wc *wc, ud_hdr_t *ud_hdr )
{
    int rc;
    uint8_t type = ud_hdr->type;
    //info_wtime(log_fp, "MSG from srv (%"PRIu8")\n", type);
    switch(type) {
        case CSM_REPLY:
        {
            //dump_bytes(log_fp, ud_hdr, wc->byte_len - 40, "received bytes");
            /* CSM reply from server */
            //info(log_fp, ">> Received CSM reply from server with lid%"
            //    PRIu16"\n", wc->slid);
            /* Handle reply */
            rc = handle_csm_reply(wc, (client_rep_t*)ud_hdr);
            if (0 != rc) {
                error(log_fp, "Cannot handle reply from server\n");
                type = MSG_ERROR;
            }
            break;
        }
        case CFG_REPLY:
        {
            /* PSM reply from server */
            info(log_fp, ">> Received CFG_REPLY from server with lid%"
                PRIu16"\n", wc->slid);
            break;
        }
        default:
        {
            //debug(log_fp, "Unknown message\n");
        }

    }
    return type;
}

#endif 

/* ================================================================== */
/* Joining the group */
#if 1

int ud_join_cluster()
{
    uint64_t req_id = IBDEV->request_id; // unique REQ ID

    ud_hdr_t *request = (ud_hdr_t*)IBDEV->ud_send_buf;
    uint32_t len = sizeof(ud_hdr_t);
    
    memset(request, 0, len);
    request->id = req_id;
    request->type = JOIN;

    debug(log_fp, "# Sending JOIN request\n");
    return mcast_send_message(len);
}

/**
 * Handle a join request from a server
 */
static int
handle_server_join_request( struct ibv_wc *wc, ud_hdr_t *request )
{
    int rc;
    uint8_t i, size, empty;
    
    /* Case 1: Transitional configuration */
    if (CID_STABLE != SRV_DATA->config.cid.state) {
        /* Wait until the resize is done; 
         * the join request will repeat later */
        return 0;
    }
    size = SRV_DATA->config.cid.size[0];
    empty = size;
    
    /* Find the ep that send this request; look in the EP DB */
    dare_ep_t *ep = ep_search(&SRV_DATA->endpoints, wc->slid);
    if (ep == NULL) {
        /* No ep with this LID; create a new one */
        ep = ep_insert(&SRV_DATA->endpoints, wc->slid);
    }
    ep->ud_ep.qpn = wc->src_qp;

    /* Find first empty entry; or maybe I already reply to this "client" */
    dare_ib_ep_t *ib_ep;
    for (i = size-1; i < size; i--) {
        if (CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) {
            ib_ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
            if (ib_ep->ud_ep.lid == wc->slid) {
                /* There is already a server with this LID in 
                the configuration; check if the JOIN request is repeated
                TODO should get last_req_id from a protocol SM !!! */
                if (ep->last_req_id == request->id) {
                    /* I received this request before; 
                    check if the entry is committed */
                    if (!ep->committed) return 0;
                    /* Probably the reply got lost (UD) */
                    rc = ud_send_clt_reply(wc->slid, request->id, CONFIG);
                    if (0 != rc) {
                        error_return(1, log_fp, "Cannot send client reply\n");
                    }
                    return 0;
                }
                /* New JOIN request; ingnore
                Case 2: Server fails, recovers and joins before leader 
                suspects the failure */
                return 0;
            }
            continue;
        }
        empty = i;
    }
    /* If (emtpy != size) Case 3: Empty place (i.e., BITMASK < 2^size-1) 
    else 
    Case 4: The group is full (i.e., BITMASK = 2^size-1) - upsize 
        i.   [size,0,STABLE,2^size-1] -> [size,size+1,EXTENDED,2^(size+1)-1]
        ii.  [size,size+1,EXTENDED,2^(size+1)-1] -> [size,size+1,TRANSIT,2^(size+1)-1]
        iii. [size,size+1,TRANSIT,2^(size+1)-1] -> [size+1,0,STABLE,2^(size+1)-1] */
    dare_cid_t old_cid = SRV_DATA->config.cid;
    CID_SERVER_ADD(SRV_DATA->config.cid, empty);
    if (empty == size) {
        /* Add an extra server; the group is later up-sized */
        SRV_DATA->config.cid.state = CID_EXTENDED;
        SRV_DATA->config.cid.size[1] = SRV_DATA->config.cid.size[0] + 1;
        SRV_DATA->config.cid.epoch++;
    }
    SRV_DATA->config.req_id = request->id;
    SRV_DATA->config.clt_id = wc->slid;
    PRINT_CONF_TRANSIT(old_cid, SRV_DATA->config.cid);
    
    /* Initialize the new server */
    server_t *new_server = &SRV_DATA->config.servers[empty];
    ib_ep = (dare_ib_ep_t*)new_server->ep;
    wc_to_ud_ep(&ib_ep->ud_ep, wc);
    ib_ep->rc_connected = 0;
    new_server->fail_count = 0;
    new_server->next_lr_step = LR_GET_WRITE;
    new_server->send_flag = 1;
    new_server->send_count = 0;
    SRV_DATA->ctrl_data->vote_ack[empty] = SRV_DATA->log->len;
    SRV_DATA->ctrl_data->apply_offsets[empty] = SRV_DATA->log->head;
    
    /* Update request ID for this LID; TODO use protocol SM */
    ep->last_req_id = request->id;
    
    /* Append CONFIG entry */
    ep->cid_idx = log_append_entry(SRV_DATA->log, 
            SID_GET_TERM(SRV_DATA->ctrl_data->sid), request->id, 
            wc->slid, CONFIG, &SRV_DATA->config.cid);
    //INFO_PRINT_LOG(log_fp, SRV_DATA->log);
    info_wtime(log_fp, "Adding CONFIG entry to idx=%"PRIu64"\n", ep->cid_idx);
    
    /* Set the status of the new entry as uncommitted */
    ep->committed = 0;
              
    return 0;
}

static void 
handle_server_join_reply(struct ibv_wc *wc, reconf_rep_t *reply)
{
    if (reply->hdr.id < IBDEV->request_id) {
        /* Old reply; ignore */
        return;
    }
    IBDEV->request_id++;
    
    SRV_DATA->config.idx = reply->idx;
    SRV_DATA->config.cid = reply->cid;
    /* Server set its head offset to the one of the leader */
    SRV_DATA->log->head = reply->head;
    /* Server considers only CONFIG entries with larger indexes */
    SRV_DATA->config.cid_idx = reply->cid_idx;  
    /* Start looking for CONFIG entries starting with the head offset */
    SRV_DATA->config.cid_offset = SRV_DATA->log->head;
}

#endif

/* ================================================================== */
/* Establish RC */
#if 1

/**
 * Send connection request (mcast)
 */
int ud_exchange_rc_info()
{
    uint8_t i, j;
    dare_ib_ep_t* ep;
    uint64_t req_id = IBDEV->request_id; // unique REQ ID

    uint32_t numbytes = sizeof(rc_syn_t) +
        2*get_extended_group_size(SRV_DATA->config)*sizeof(uint32_t);
    if (mtu_value(IBDEV->mtu) < numbytes) {
        error_return(1, log_fp, "Cannot send RC INFO via mcast; not enough space\n");
    }    
    rc_syn_t *request = (rc_syn_t*)IBDEV->ud_send_buf;
    uint32_t *qpns = (uint32_t*)request->data;
    uint32_t len = sizeof(rc_syn_t);

    memset(request, 0, len);
    request->hdr.id        = req_id;
    request->hdr.type      = RC_SYN;
    request->log_rm.raddr  = (uint64_t)SRV_DATA->log;
    request->log_rm.rkey   = IBDEV->lcl_mr[LOG_QP]->rkey;
    request->ctrl_rm.raddr = (uint64_t)SRV_DATA->ctrl_data;
    request->ctrl_rm.rkey  = IBDEV->lcl_mr[CTRL_QP]->rkey;
    request->mtu           = IBDEV->mtu;
    request->idx           = SRV_DATA->config.idx;

//info(log_fp, "RC SYN: LOG MR=[%"PRIu64"; %"PRIu32"]; LOG MR=[%"PRIu64"; %"PRIu32"]\n",
//     request->log_rm.raddr, request->log_rm.rkey, request->ctrl_rm.raddr, request->ctrl_rm.rkey);
   
    
    request->size = get_extended_group_size(SRV_DATA->config);
    for (i = 0, j = 0; i < request->size; i++, j += 2) {
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        qpns[j] = ep->rc_ep.rc_qp[LOG_QP].qp->qp_num;
        qpns[j+1] = ep->rc_ep.rc_qp[CTRL_QP].qp->qp_num;
//info(log_fp, "   [%d] LOG_QPN=%"PRIu32"; CTRL_QPN=%"PRIu32"\n", i, qpns[j], qpns[j+1]);
    }
    len += 2*request->size*sizeof(uint32_t);

    //info(log_fp, ">> Sending RC SYN (mcast)\n");
    return mcast_send_message(len);
}

/**
 * If RC not known for all servers, send connection request (mcast)
 */
int ud_update_rc_info()
{
    dare_ib_ep_t *ep;
    uint8_t i, size = get_extended_group_size(SRV_DATA->config);
    
    for (i = 0; i < size; i++) {
        if (i == SRV_DATA->config.idx) continue;
        if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) continue;
        ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
        if (!ep->rc_connected) {
            break;
        }
    }
    if (i == size) {
        return 0;
    }
    text(log_fp, "PERIODIC RC UPDATE\n");    
    return ud_exchange_rc_info();
}

/**
 * Received RC_SYN msg; reply with RC_SYNACK (unicast)
 */
static int 
handle_rc_syn(struct ibv_wc *wc, rc_syn_t *msg)
{
    int rc;
    dare_ib_ep_t *ep;
    uint32_t *qpns = (uint32_t*)msg->data;
    
    if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, msg->idx)) {
        /* Configuration inconsistency; it will be solved later */
        return 0;
    }
    if (SRV_DATA->config.idx >= msg->size) {
        /* Configuration inconsistency; it will be solved later */
        return 0;
    }
    
    /* Verify if RC already established */
    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[msg->idx].ep;
    if (0 == ep->rc_connected) {
        /* Create UD endpoint from WC */
        wc_to_ud_ep(&ep->ud_ep, wc);
        text(log_fp, "New SYN msg from server %"PRIu8" with lid=%"PRIu16"\n", 
                msg->idx, ep->ud_ep.lid);
        
        /* Set log and ctrl memory region info */
        ep->rc_ep.rmt_mr[LOG_QP].raddr  = msg->log_rm.raddr;
        ep->rc_ep.rmt_mr[LOG_QP].rkey   = msg->log_rm.rkey;
        ep->rc_ep.rmt_mr[CTRL_QP].raddr = msg->ctrl_rm.raddr;
        ep->rc_ep.rmt_mr[CTRL_QP].rkey  = msg->ctrl_rm.rkey;

        /* Set the remote QPNs */
        ep->rc_ep.rc_qp[LOG_QP].qpn = qpns[2*SRV_DATA->config.idx];
        ep->rc_ep.rc_qp[CTRL_QP].qpn = qpns[2*SRV_DATA->config.idx+1];
        
        /* Set MTU for this connection */
        ep->mtu = msg->mtu > IBDEV->mtu ? IBDEV->mtu : msg->mtu;

#if 0        
        info(log_fp, "[%02"PRIu8"]  log: "
                         "RQPN=%"PRIu32"; "
                         "RMR=[%"PRIu64"; %"PRIu32"]\n",
                          msg->idx, 
                          ep->rc_ep.rc_qp[LOG_QP].qpn, 
                          ep->rc_ep.rmt_mr[LOG_QP].raddr, 
                          ep->rc_ep.rmt_mr[LOG_QP].rkey);
        info(log_fp, "     ctrl: "
                         "RQPN=%"PRIu32"; "
                         "RMR=[%"PRIu64"; %"PRIu32"]\n",
                          ep->rc_ep.rc_qp[CTRL_QP].qpn, 
                          ep->rc_ep.rmt_mr[CTRL_QP].raddr, 
                          ep->rc_ep.rmt_mr[CTRL_QP].rkey);
#endif        


        /* Connect CTRL QP */ 
        rc = rc_connect_server(msg->idx, CTRL_QP);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot connect server (CTRL)\n");
        }
        if (SID_GET_L(SRV_DATA->ctrl_data->sid) && 
             SID_GET_IDX(SRV_DATA->ctrl_data->sid) == SRV_DATA->config.idx)
        {
            /* I'm the leader -- connect LOG QP */ 
            rc = rc_connect_server(msg->idx, LOG_QP);
            if (0 != rc) {
                error_return(1, log_fp, "Cannot connect server (LOG)\n");
            }
        }
        info_wtime(log_fp, "New connection: #%"PRIu8"\n", msg->idx);
    }    
    
    /* Send RC_SYNACK msg */  
    rc_syn_t *reply = (rc_syn_t*)IBDEV->ud_send_buf;
    qpns = (uint32_t*)reply->data;
    uint32_t len = sizeof(rc_syn_t);
    memset(reply, 0, len);
    reply->hdr.id        = msg->hdr.id;
    reply->hdr.type      = RC_SYNACK;
    reply->log_rm.raddr  = (uint64_t)SRV_DATA->log;
    reply->log_rm.rkey   = IBDEV->lcl_mr[LOG_QP]->rkey;
    reply->ctrl_rm.raddr = (uint64_t)SRV_DATA->ctrl_data;
    reply->ctrl_rm.rkey  = IBDEV->lcl_mr[CTRL_QP]->rkey;
    reply->mtu           = IBDEV->mtu;
    reply->idx           = SRV_DATA->config.idx;
    reply->size          = 1;
    qpns[0] = ep->rc_ep.rc_qp[LOG_QP].qp->qp_num;
    qpns[1] = ep->rc_ep.rc_qp[CTRL_QP].qp->qp_num;
    len += 2*sizeof(uint32_t);
    //info(log_fp, ">> Sending back RC SYNACK msg\n");
    rc = ud_send_message(&ep->ud_ep, len);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot send UD message\n");
    }
    return 0;
}

/**
 * Received RC_SYNACK msg; reply with RC_ACK (unicast)
 * Note: now we can use the CTRL QP with this server
 */
static int 
handle_rc_synack(struct ibv_wc *wc, rc_syn_t *msg)
{
    int rc, ret = 0;
    dare_ib_ep_t *ep;
    uint32_t *qpns = (uint32_t*)msg->data;

    if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, msg->idx)) {
        /* Configuration inconsistency; it will be solved later */
        return 0;
    }

    
    /* Verify if RC already established */
    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[msg->idx].ep;
    if (0 == ep->rc_connected) {
        /* Create UD endpoint from WC */
        wc_to_ud_ep(&ep->ud_ep, wc);
        debug(log_fp, "NEW SYNACK msg from server %"PRIu8" with lid=%"PRIu16"\n", 
                msg->idx, ep->ud_ep.lid);

        /* Set log and ctrl memory region info */
        ep->rc_ep.rmt_mr[LOG_QP].raddr  = msg->log_rm.raddr;
        ep->rc_ep.rmt_mr[LOG_QP].rkey   = msg->log_rm.rkey;
        ep->rc_ep.rmt_mr[CTRL_QP].raddr = msg->ctrl_rm.raddr;
        ep->rc_ep.rmt_mr[CTRL_QP].rkey  = msg->ctrl_rm.rkey;

        /* Set the remote QPNs */
        ep->rc_ep.rc_qp[LOG_QP].qpn    = qpns[0];
        ep->rc_ep.rc_qp[CTRL_QP].qpn   = qpns[1];
        
        /* Set MTU for this connection */
        ep->mtu = msg->mtu > IBDEV->mtu ? IBDEV->mtu : msg->mtu;
        
        /* Mark RC established */
        ep->rc_connected = 1;
#if 0
        info(log_fp, "[%02"PRIu8"]  log: "
                         "RQPN=%"PRIu32"; "
                         "RMR=[%"PRIu64"; %"PRIu32"]\n",
                          msg->idx, 
                          ep->rc_ep.rc_qp[LOG_QP].qpn, 
                          ep->rc_ep.rmt_mr[LOG_QP].raddr, 
                          ep->rc_ep.rmt_mr[LOG_QP].rkey);
        info(log_fp, "     ctrl: "
                         "RQPN=%"PRIu32"; "
                         "RMR=[%"PRIu64"; %"PRIu32"]\n",
                          ep->rc_ep.rc_qp[CTRL_QP].qpn, 
                          ep->rc_ep.rmt_mr[CTRL_QP].raddr, 
                          ep->rc_ep.rmt_mr[CTRL_QP].rkey);
#endif 

        /* Connect only CTRL QP */ 
        rc = rc_connect_server(msg->idx, CTRL_QP);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot connect server (CTRL)\n");
        }
        if (SID_GET_L(SRV_DATA->ctrl_data->sid) && 
             SID_GET_IDX(SRV_DATA->ctrl_data->sid) == SRV_DATA->config.idx)
        {
            /* I'm the leader -- connect LOG QP */ 
            rc = rc_connect_server(msg->idx, LOG_QP);
            if (0 != rc) {
                error_return(1, log_fp, "Cannot connect server (LOG)\n");
            }
        }
        info_wtime(log_fp, "New connection: #%"PRIu8"\n", msg->idx);

        /* Verify the number of RC established; if RC established with at 
         * least half of the group, then we can proceed further */
        uint8_t i; 
        uint8_t connections = 0;
        uint8_t size = get_group_size(SRV_DATA->config);
        for (i = 0; i < size; i++) {
            if (i == SRV_DATA->config.idx) continue;
            if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) continue;
            ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
            if (ep->rc_connected) {
                connections++;
            }
        }
        /* Note: I'm not included */
        if (connections >= size / 2) {
        //if (connections > size / 2) {
            ret = REQ_MAJORITY;
        }
    }
    
    /* Send RC_ACK msg */
    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[msg->idx].ep;
    rc_ack_t *reply = (rc_ack_t*)IBDEV->ud_send_buf;
    uint32_t len = sizeof(rc_ack_t);
    memset(reply, 0, len);
    reply->hdr.id   = msg->hdr.id;
    reply->hdr.type = RC_ACK;
    reply->idx      = SRV_DATA->config.idx;
    //info(log_fp, ">> Sending back RC ACK msg\n");
    rc = ud_send_message(&ep->ud_ep, len);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot send UD message\n");
    }
    
    return ret;
}

/**
 * Received RC_ACK msg
 * Note: now we can use the CTRL QP with this server
 */
static int 
handle_rc_ack(struct ibv_wc *wc, rc_ack_t *msg)
{
    uint8_t i;
    dare_ib_ep_t *ep;

    if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, msg->idx)) {
        /* Configuration inconsistency; it will be solved later */
        return 0;
    }
    
    /* Verify if RC already established */
    ep = (dare_ib_ep_t*)SRV_DATA->config.servers[msg->idx].ep;
    if (0 == ep->rc_connected) {
        /* Mark RC established */
        ep->rc_connected = 1;
        
        /* Verify the number of RC established; if RC established with at 
         * least half of the group, then we can proceed further */
        uint8_t connections = 0;
        uint8_t size = get_group_size(SRV_DATA->config);
        for (i = 0; i < size; i++) {
            if (i == SRV_DATA->config.idx) continue;
            if (!CID_IS_SERVER_ON(SRV_DATA->config.cid, i)) continue;
            ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
            if (ep->rc_connected) {
                connections++;
            }
        }
        /* Note: I'm not included */
        if (connections < size / 2) {
        //if (connections < size - 1) {
            return 0;
        }
        return REQ_MAJORITY;
    }
    return 0;
}

#endif

/* ================================================================== */
/* Client messages */
#if 1

/**
 * Apply the next SM CMD to the local SM 
 * (done here because of the ud_send_buf)
 */
int ud_apply_cmd_locally()
{
    size_t bytes_read;
    kvs_cmd_t *kvs_cmd;
    
    client_req_t *request = (client_req_t*)IBDEV->ud_send_buf;
    uint32_t len = sizeof(client_req_t);
    memset(request, 0, len);
    
    //request->hdr.id = ++IBDEV->request_id;
    
    switch (CLT_DATA->input->sm_type) {
        case CLT_NULL:
            // TODO:
            break;  
        case CLT_KVS:
            /**
             * Trace file format (KVS): 
             *      READ/WRITE | PUT/GET/DELETE | KEY | LEN | DATA 
             */
            /* Read CMD type: read or write */
            bytes_read = fread(&request->hdr.type, 1, 1, CLT_DATA->trace_fp);
            if (bytes_read < 0) {
                error_return(1, log_fp, "Cannot read trace file because %s\n",
                            strerror(errno));
            }
            if (bytes_read == 0) {
                /* No more commands in the trace */
                return -1;
            }

            /* Read KVS CMD without possible data */
            kvs_cmd = (kvs_cmd_t*)request->cmd.cmd;
            bytes_read = fread(kvs_cmd, sizeof(kvs_cmd_t), 1, 
                                CLT_DATA->trace_fp);
            if (bytes_read < 0) {
                error_return(1, log_fp, "Cannot read trace file because %s\n",
                            strerror(errno));
            }
            if (bytes_read == 0) {
                /* No more commands in the trace */
                return -1;
            }
            /* Set partial length */
            request->cmd.len = bytes_read * sizeof(kvs_cmd_t);
            len += request->cmd.len;
            
            //info(log_fp, "\nTrace cmd: [%s] %s key=%s; data len=%"PRIu16"\n", 
            //    (request->hdr.type == CSM_READ) ? "READ" : "WRITE", 
            //    (kvs_cmd->type == KVS_PUT) ? "PUT" : 
            //    (kvs_cmd->type == KVS_GET) ? "GET" : "RM", 
            //    kvs_cmd->key, kvs_cmd->len);
            
            /* Read possible data */
            if (len + kvs_cmd->len > mtu_value(IBDEV->mtu)) {
                // TODO: in the future, implement segmentation
                error_return(1, log_fp, "Cannot send commands larger "
                "than %d\n", mtu_value(IBDEV->mtu));
            }
            if (kvs_cmd->len > 0) {
                bytes_read = fread(kvs_cmd->data, kvs_cmd->len, 1, 
                                    CLT_DATA->trace_fp);
                if (bytes_read < 0) {
                    error_return(1, log_fp, "Cannot read trace file because %s\n",
                                strerror(errno));
                }
                if (bytes_read == 0) {
                    /* No more commands in the trace */
                    return -1;
                }
            }
            request->cmd.len += kvs_cmd->len;
            len += kvs_cmd->len;
            break;
        case CLT_FS:
            // TODO:
            break;
    }
    
    if (request->hdr.type == CSM_READ) {
        client_rep_t *reply = (client_rep_t*)IBDEV->ud_send_buf;
        CLT_DATA->sm->apply_cmd(CLT_DATA->sm, &request->cmd, &reply->data);
        switch (CLT_DATA->input->sm_type) {
            case CLT_NULL:
                // TODO:
                break;  
            case CLT_KVS:
                if (reply->data.len != 0) {
                    //debug(log_fp, "Retrieved data: %.*s\n", 
                    //     reply->data.len, reply->data.data);
                }
                break;
            case CLT_FS:
                // TODO:
                break;
        }

    }
    else {
        CLT_DATA->sm->apply_cmd(CLT_DATA->sm, &request->cmd, NULL);
    }
    
    
    return 0;
}

/** 
 * Create a client request
 *  - read a command from the trace file
 *  - create client_req_t structure
 *  - send request to server group
 */
int ud_create_clt_request()
{
    size_t bytes_read;
    kvs_cmd_t *kvs_cmd;
    
    client_req_t *csm_req = (client_req_t*)IBDEV->ud_send_buf;
    reconf_req_t *psm_req = (reconf_req_t*)IBDEV->ud_send_buf;
    ud_hdr_t *hdr = (ud_hdr_t*)IBDEV->ud_send_buf;
    uint32_t len;
    
    /* Set request id */
    hdr->id = ++IBDEV->request_id;
    
    /* Read CMD type: CSM_WRITE, CSM_READ, DOWNSIZE */
    bytes_read = fread(&hdr->type, 1, 1, CLT_DATA->trace_fp);
    if (bytes_read < 0) {
        error_return(1, log_fp, "Cannot read trace file because %s\n",
                    strerror(errno));
    }
    if (bytes_read == 0) {
        /* No more commands in the trace */
        return -1;
    }
    
    if (DOWNSIZE == hdr->type) {
        /* Resize CMD: size */
        len = sizeof(reconf_req_t);
        bytes_read = fread(&psm_req->idx_size, 1, 1, CLT_DATA->trace_fp);
        if (bytes_read < 0) {
            error_return(1, log_fp, "Cannot read trace file because %s\n",
                        strerror(errno));
        }
        if (bytes_read == 0) {
            /* No more commands in the trace */
            return -1;
        }
        goto send_request;
    }
    
    len = sizeof(client_req_t);
    switch (CLT_DATA->input->sm_type) {
        case CLT_NULL:
            // TODO:
            break;  
        case CLT_KVS: /* KVS CMD: PUT/GET/DELETE | KEY | LEN | DATA */
            /* Read KVS CMD without possible data */
            kvs_cmd = (kvs_cmd_t*)csm_req->cmd.cmd;
            bytes_read = fread(kvs_cmd, sizeof(kvs_cmd_t), 1, 
                                CLT_DATA->trace_fp);
            if (bytes_read < 0) {
                error_return(1, log_fp, "Cannot read trace file because %s\n",
                            strerror(errno));
            }
            if (bytes_read == 0) {
                /* No more commands in the trace */
                return -1;
            }
            /* Set partial length */
            csm_req->cmd.len = bytes_read * sizeof(kvs_cmd_t);
            len += csm_req->cmd.len;
            
            //info(log_fp, "\nTrace cmd: [%s] %s key=%s; data len=%"PRIu16"\n", 
            //    (hdr->type == CSM_READ) ? "READ" : "WRITE", 
            //    (kvs_cmd->type == KVS_PUT) ? "PUT" : 
            //    (kvs_cmd->type == KVS_GET) ? "GET" : "RM", 
            //    kvs_cmd->key, kvs_cmd->len);
            
            /* Read possible data */
            if (len + kvs_cmd->len > mtu_value(IBDEV->mtu)) {
                // TODO: in the future, implement segmentation
                error_return(1, log_fp, "Cannot send commands larger "
                "than %d\n", mtu_value(IBDEV->mtu));
            }
            if (kvs_cmd->len > 0) {
                bytes_read = fread(kvs_cmd->data, kvs_cmd->len, 1, 
                                    CLT_DATA->trace_fp);
                if (bytes_read < 0) {
                    error_return(1, log_fp, "Cannot read trace file because %s\n",
                                strerror(errno));
                }
                if (bytes_read == 0) {
                    /* No more commands in the trace */
                    return -1;
                }
            }
            csm_req->cmd.len += kvs_cmd->len;
            len += kvs_cmd->len;
            break;
        case CLT_FS:
            // TODO:
            break;
    }
   
send_request:
    /* Send request */
    if (CLT_TYPE_RTRACE == CLT_DATA->input->clt_type) {
        HRT_GET_TIMESTAMP(CLT_DATA->t1);
    }
    return send_clt_request(len);
}

/** 
 * Resend the request present in the buffer
 */
int ud_resend_clt_request()
{   
    ud_hdr_t *hdr = (ud_hdr_t*)IBDEV->ud_send_buf;
    
    /* Compute length */
    uint32_t len;
    if (DOWNSIZE == hdr->type) {
        len = sizeof(reconf_req_t);
    }
    else {
        len = sizeof(client_req_t) 
            + ((client_req_t*)IBDEV->ud_send_buf)->cmd.len;
    }
    /* Reset the leader: go back to multicast */
    ud_ep_t *ud_ep = (ud_ep_t*)CLT_DATA->leader_ep;
    if (0 != ud_ep->lid) {
        info_wtime(log_fp, "Server %"PRIu16" is unresponsive;"
                    " switch back to mcast\n", ud_ep->lid);
       ud_ep->lid = 0;
    }
    
    /* Send request */
    return send_clt_request(len);
}

/** 
 * Send the request present in the buffer to the leader 
 * if the leader is unknown, mcast the request
 */
static int 
send_clt_request( uint32_t len )
{
    int rc;
    
    /* If needed, alloc memory for leader endpoint */
    if (NULL == CLT_DATA->leader_ep) {
        CLT_DATA->leader_ep = malloc(sizeof(ud_ep_t));
        memset(CLT_DATA->leader_ep, 0, sizeof(ud_ep_t));
    }
    ud_ep_t *ud_ep = (ud_ep_t*)CLT_DATA->leader_ep;

    /* Send request */
    //ud_ep->lid = 0;
    if (0 != ud_ep->lid) {
        /* There is a known leader */
        rc = ud_send_message(ud_ep, len);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot send message over UD "
                        "to server %"PRIu16"\n", ud_ep->lid);
        }
    }
    else {
        /* No leader known */
        rc = mcast_send_message(len);
        if (0 != rc) {
            error_return(1, log_fp, "Cannot send message over mcast\n");
        }
    }

    return 0;
}


void ud_clt_answer_read_request(dare_ep_t *ep)
{
    int rc;
    ep->wait_for_idx = 0;
    client_req_t *request = (client_req_t*)ep->last_read_request;
    
    /* Create reply */
    client_rep_t *reply = (client_rep_t*)IBDEV->ud_send_buf;
    memset(reply, 0, sizeof(client_rep_t));
    reply->hdr.id = request->hdr.id;
    reply->hdr.type = CSM_REPLY;
    
    /* Get data from SM */
    rc = SRV_DATA->sm->apply_cmd(SRV_DATA->sm, &request->cmd, &reply->data);
    if (0 != rc) {
        error(log_fp, "Cannot apply read operation to the state machine\n");
    }
    
    /* Send reply */
    uint32_t len = sizeof(client_rep_t) + reply->data.len;
    rc = ud_send_message(&ep->ud_ep, len);
    if (0 != rc) {
        error(log_fp, "Cannot send message over UD to %"PRIu16"\n", 
                     ep->ud_ep.lid);
    }
}

int ud_send_clt_reply( uint16_t lid, uint64_t req_id, uint8_t type )
{
    int rc;
    dare_ib_ep_t *ib_ep;
    
    client_rep_t *csm_reply;
    reconf_rep_t *psm_reply;
    uint32_t len;
    uint8_t i;
    
    /* Find the ep that send this request */
    dare_ep_t *ep = ep_search(&SRV_DATA->endpoints, lid);
    if (ep == NULL) {
        /* No ep with this LID; create a new one */
        ep = ep_insert(&SRV_DATA->endpoints, lid);
        ep->last_req_id = 0;
    }
    // TODO: you should get the last_req_id from the protocol SM
    ep->last_req_id = req_id;
    ep->committed = 1;
    
    /* Create reply */
    switch(type) {
        case CSM:
            /* Reply to a ClientSM request */
            csm_reply = (client_rep_t*)IBDEV->ud_send_buf;
            memset(csm_reply, 0, sizeof(client_rep_t));
            // TODO: you should get the last_req_id from the protocol SM
            csm_reply->hdr.id = req_id;
            csm_reply->hdr.type = CSM_REPLY;
            csm_reply->data.len = 0;
            len = sizeof(client_rep_t);
#ifdef WRITE_BENCH            
            //HRT_GET_TIMESTAMP(SRV_DATA->t2);
            HRT_GET_ELAPSED_TICKS(SRV_DATA->t1, SRV_DATA->t2, &ticks[measure_count]);
            measure_count++;
            if (measure_count == 1000) {
                qsort(ticks, measure_count, sizeof(uint64_t), cmpfunc_uint64);
                info(log_fp, "Write request time: %lf (%lf, %lf)\n", 
                    HRT_GET_USEC(ticks[measure_count/2]), HRT_GET_USEC(ticks[19]), 
                    HRT_GET_USEC(ticks[measure_count-21]));
                info(log_fp, "Sent %"PRIu32" bytes\n", len);
                measure_count = 0;
            }
#endif             
            break;
        case CONFIG:
            /* Reply to a reconfiguration request */
            psm_reply = (reconf_rep_t*)IBDEV->ud_send_buf;
            memset(psm_reply, 0, sizeof(reconf_rep_t));
            psm_reply->hdr.id = req_id;
            psm_reply->hdr.type = CFG_REPLY;
            psm_reply->cid = SRV_DATA->config.cid;
            psm_reply->cid_idx = ep->cid_idx;
            psm_reply->head = SRV_DATA->log->head;
            uint8_t size = get_extended_group_size(SRV_DATA->config);
            for (i = 0; i < size; i++) {
                ib_ep = (dare_ib_ep_t*)SRV_DATA->config.servers[i].ep;
                if (NULL == ib_ep) continue;
                if (ib_ep->ud_ep.lid == lid) break;
            }
            psm_reply->idx = i;
            len = sizeof(reconf_rep_t);
            break;
    }
    /* Send reply */
    rc = ud_send_message(&ep->ud_ep, len);
    if (0 != rc) {
        error_return(1, log_fp, "Cannot send message over UD to %"PRIu16"\n", 
                     lid);
    }
#ifdef WRITE_BENCH            
   // HRT_GET_TIMESTAMP(SRV_DATA->t1);
#endif
    
    return 0;
}

static int 
handle_csm_reply(struct ibv_wc *wc, client_rep_t *reply)
{
    if (reply->hdr.id < IBDEV->request_id) {
        /* Old reply; ignore */
        return 0;
    }
    
    ud_ep_t *ud_ep = (ud_ep_t*)CLT_DATA->leader_ep;
    if (ud_ep->lid != wc->slid) {
        /* New leader: set the UD endpoint data */
        //info_wtime(log_fp, "Reply from diffrent LID: %"PRIu16" vs. %"PRIu16"\n", ud_ep->lid, wc->slid);
        wc_to_ud_ep(ud_ep, wc);
        //info_wtime(log_fp, "New group leader: %"PRIu16" (type=%"PRIu8")\n", ud_ep->lid, reply->hdr.type);
    }
    
    if (reply->data.len != 0) {
        debug(log_fp, "Received data: %.*s\n", 
            reply->data.len, reply->data.data);
    }
    
    return 0;
}

#endif

/* ================================================================== */
/* Reconfiguration */
#if 1

/** 
 * Create a client request to downsize the group of servers
 */
int ud_create_clt_downsize_request()
{
    reconf_req_t *request = (reconf_req_t*)IBDEV->ud_send_buf;
    ud_hdr_t *hdr = (ud_hdr_t*)IBDEV->ud_send_buf;
    uint32_t len = sizeof(reconf_req_t);
    
    /* Set request id */
    hdr->id = ++IBDEV->request_id;
    
    /* Set the type of the request */
    hdr->type = DOWNSIZE;
    
    /* Set the new group size */
    request->idx_size = CLT_DATA->input->group_size;
    
    /* Send request */
    return send_clt_request(len);
}

static void
handle_downsize_request(struct ibv_wc *wc, reconf_req_t *request)
{
    if (CID_STABLE != SRV_DATA->config.cid.state) {
        /* Cannot downsize an unstable group */
        return;
    }
    if (request->idx_size > SRV_DATA->config.cid.size[0]) {
        /* We do not support upsize operations - see JOIN */
        return; 
    }
    
    /* Find the ep that send this request */
    dare_ep_t *ep = ep_search(&SRV_DATA->endpoints, wc->slid);
    if (ep == NULL) {
        /* No ep with this LID; create a new one */
        ep = ep_insert(&SRV_DATA->endpoints, wc->slid);
    }
    ep->ud_ep.qpn = wc->src_qp;
    // TODO: you should get the last_req_id from the protocol SM
    ep->last_req_id = request->hdr.id;
    ep->committed = 0;
    
    if (request->idx_size != SRV_DATA->config.cid.size[0]) {
        dare_cid_t old_cid = SRV_DATA->config.cid;
        SRV_DATA->config.cid.size[1] = request->idx_size;
        SRV_DATA->config.cid.state = CID_TRANSIT;
        SRV_DATA->config.cid.epoch++;
        SRV_DATA->config.req_id = request->hdr.id;
        SRV_DATA->config.clt_id = wc->slid;
        PRINT_CONF_TRANSIT(old_cid, SRV_DATA->config.cid);
        /* Append CONFIG entry */
        log_append_entry(SRV_DATA->log, SID_GET_TERM(SRV_DATA->ctrl_data->sid), 
                request->hdr.id, wc->slid, CONFIG, &SRV_DATA->config.cid);
        return;
    }
    
    /* The cluster has already the required size */ 
    ud_send_clt_reply(wc->slid, request->hdr.id, CONFIG);
}

#endif

/* ================================================================== */
/* LogGP */
#if 1

static int 
cmpfunc_uint64( const void *a, const void *b )
{
    return ( *(uint64_t*)a - *(uint64_t*)b );
}
#define MEASURE_COUNT 1000
#define LOGGP_UD_IBV

double ud_loggp_prtt( int n, double delay, uint32_t size, int inline_flag )
{
    int rc, i, j, count, ne;
    uint8_t rmt = !SRV_DATA->config.idx;
    ud_hdr_t *request = (ud_hdr_t*)IBDEV->ud_send_buf;
    ud_hdr_t *ud_hdr;
    uint64_t req_id = IBDEV->request_id; // unique REQ ID
    dare_ib_ep_t *ep = (dare_ib_ep_t*)SRV_DATA->config.servers[rmt].ep;
    struct ibv_wc *wc = wc_array;
    
    struct ibv_sge sg;
    struct ibv_send_wr wr;
    struct ibv_send_wr *bad_wr = NULL;

    HRT_TIMESTAMP_T t1, t2;
    uint64_t ticks[MEASURE_COUNT];
    double usecs;
    loggp_not_inline = !inline_flag;
    
    if (size > mtu_value(IBDEV->mtu)) {
        error_return(0, log_fp, "Maximum buffer size for LogGP UD is %"PRIu32"\n", 
                    mtu_value(IBDEV->mtu));
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
    
    memset(request, 0, size);
    request->id = req_id;
    request->type = LOGGP_UD;
#ifdef LOGGP_UD_IBV
    memset(&sg, 0, sizeof(sg));
    sg.addr   = (uint64_t)IBDEV->ud_send_buf;
    sg.length = size;
    sg.lkey   = IBDEV->ud_send_mr->lkey;
     
    memset(&wr, 0, sizeof(wr));
    wr.wr_id      = 0;
    wr.sg_list    = &sg;
    wr.num_sge    = 1;
    wr.opcode     = IBV_WR_SEND;
    wr.send_flags = IBV_SEND_SIGNALED;
    if ( (size <= IBDEV->rc_max_inline_data) && !loggp_not_inline ) {
        wr.send_flags |= IBV_SEND_INLINE;
    }
    wr.wr.ud.ah          = ep->ud_ep.ah;
    wr.wr.ud.remote_qpn  = ep->ud_ep.qpn;
    wr.wr.ud.remote_qkey = 0;
#endif
    
    for (j = 0; j < MEASURE_COUNT; j++) {
        HRT_GET_TIMESTAMP(t1);
        for (i = 0; i < n - 1; i++) {
            if (0 == SRV_DATA->config.idx) {
                /* Initiator */
#ifdef LOGGP_UD_IBV
                rc = ibv_post_send(IBDEV->ud_qp, &wr, &bad_wr);
                if (0 != rc) {
                    error_return(1, log_fp, "ibv_post_send failed because %s\n", strerror(rc));
                }
#else                
                rc = ud_send_message(&ep->ud_ep, size);
                if (0 != rc) {
                    error_return(1, log_fp, "Cannot send UD message\n");
                }
#endif                
                /* Wait delay ms */
                //if (delay > 0) usecs_wait(delay);
            }
            else {
                while(1) {
                    ne = ibv_poll_cq(IBDEV->ud_rcq, 1, wc);
                    if (ne < 0) {
                        error_return(MSG_ERROR, log_fp, "Couldn't poll completion queue\n");
                    }
                    if (ne == 0) {
                        continue;
                    }
                    if (wc->opcode & IBV_WC_RECV) {
                        ud_post_one_receive(wc->wr_id);
                        break;
                    }
                    ud_post_one_receive(wc->wr_id);
                }
            }
        }
        if (0 == SRV_DATA->config.idx) {
            /* Initiator */
#ifdef LOGGP_UD_IBV
            rc = ibv_post_send(IBDEV->ud_qp, &wr, &bad_wr);
            if (0 != rc) {
                error_return(1, log_fp, "ibv_post_send failed because %s\n", strerror(rc));
            }
#else            
            rc = ud_send_message(&ep->ud_ep, size);
            if (0 != rc) {
                error_return(1, log_fp, "Cannot send UD message\n");
            }
#endif            
            while(1) {
                ne = ibv_poll_cq(IBDEV->ud_rcq, 1, wc);
                if (ne < 0) {
                    error_return(MSG_ERROR, log_fp, "Couldn't poll completion queue\n");
                }
                if (ne == 0) {
                    continue;
                }
                if (wc->opcode & IBV_WC_RECV) {
                    ud_post_one_receive(wc->wr_id);
                    break;
                }
                ud_post_one_receive(wc->wr_id);
            } 
        }
        else {
            while(1) {
                ne = ibv_poll_cq(IBDEV->ud_rcq, 1, wc);
                if (ne < 0) {
                    error_return(MSG_ERROR, log_fp, "Couldn't poll completion queue\n");
                }
                if (ne == 0) {
                    continue;
                }
                if (wc->opcode & IBV_WC_RECV) {
                    ud_post_one_receive(wc->wr_id);
                    break;
                }
                ud_post_one_receive(wc->wr_id);
            }
#ifdef LOGGP_UD_IBV
            rc = ibv_post_send(IBDEV->ud_qp, &wr, &bad_wr);
            if (0 != rc) {
                error_return(1, log_fp, "ibv_post_send failed because %s\n", strerror(rc));
            }
#else            
            rc = ud_send_message(&ep->ud_ep, size);
            if (0 != rc) {
                error_return(1, log_fp, "Cannot send UD message\n");
            }
#endif            
        }
        HRT_GET_TIMESTAMP(t2);
        HRT_GET_ELAPSED_TICKS(t1, t2, &ticks[j]);
#ifdef LOGGP_UD_IBV
        if (0 == SRV_DATA->config.idx) {
            for (i = 0; i < n ; i++) {
                while(1) {
                    ne = ibv_poll_cq(IBDEV->ud_scq, 1, wc);
                    if (ne < 0) {
                        error_return(MSG_ERROR, log_fp, "Couldn't poll completion queue\n");
                    }
                    if (ne == 0) {
                        continue;
                    }
                    break;
                }
            }
        }
        else {
           while(1) {
               ne = ibv_poll_cq(IBDEV->ud_scq, 1, wc);
               if (ne < 0) {
                    error_return(MSG_ERROR, log_fp, "Couldn't poll completion queue\n");
                }
                if (ne == 0) {
                    continue;
                }
                break;
            }
        }
#endif        
    }
    qsort(ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
    usecs = HRT_GET_USEC(ticks[MEASURE_COUNT/2]);
    //info(log_fp, "median=%lf, min=%lf, max=%lf\n", usecs, HRT_GET_USEC(ticks[0]), HRT_GET_USEC(ticks[MEASURE_COUNT-1]));
    return usecs;
}

#endif

/* ================================================================== */

static int
wc_to_ud_ep(ud_ep_t *ud_ep, struct ibv_wc *wc)
{
    ud_ep->lid = wc->slid;
    /* Create new AH for this LID */
    if (NULL != ud_ep->ah) {
        /* AH already created - destroy to be on the safe side */
        ud_ah_destroy(ud_ep->ah);
    }
    ud_ep->ah = ud_ah_create(ud_ep->lid);
    if (NULL == ud_ep->ah) {
        error_return(1, log_fp, "Cannot create AH for LID %"PRIu16"\n", 
                     ud_ep->lid);
    }
    ud_ep->qpn = wc->src_qp;
    return 0;
}
