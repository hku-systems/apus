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


#include "../include/dare/dare_ibv_ud.h"
#include "../include/dare/dare_ibv_rc.h"
#include "../include/dare/dare_ibv.h"
#include "../include/dare/dare_ep_db.h"
#include "../include/dare/dare_server.h"
#include "../include/dare/dare_client.h"
#include "../include/dare/timer.h"

#include "../include/dare/message.h"

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
handle_message_from_client( struct ibv_wc *wc, ud_hdr_t *ud_hdr );
static int
handle_server_join_request(struct ibv_wc *wc, ud_hdr_t *request);
static int 
handle_rc_syn(struct ibv_wc *wc, rc_syn_t *msg);
static int 
handle_rc_synack(struct ibv_wc *wc, rc_syn_t *msg);
static int 
handle_rc_ack(struct ibv_wc *wc, rc_ack_t *msg);
static int 
send_clt_request( uint32_t len );
static void 
handle_server_join_reply(struct ibv_wc *wc, reconf_rep_t *reply);

static int
wc_to_ud_ep(ud_ep_t *ud_ep, struct ibv_wc *wc, union ibv_gid dest_gid, uint16_t slid);
static uint16_t
get_unique_slid();

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

struct ibv_ah* ud_ah_create( uint16_t dlid, union ibv_gid dgid )
{
    struct ibv_ah *ah = NULL;
    struct ibv_ah_attr ah_attr;
     
    memset(&ah_attr, 0, sizeof(ah_attr));
     
    ah_attr.is_global     = 1;
    ah_attr.grh.dgid      = dgid;
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
    
    uint8_t raw[16];
    global_mgid = getenv("mgid");
    if (!global_mgid || strcmp(global_mgid, "") == 0) {
    	uint8_t mcg_gid[16] = MCG_GID;
    	memcpy(raw, mcg_gid, 16);
    }
    else {
    	if (ipv6_string_to_raw(global_mgid, raw)) {
    		error_return(1, log_fp, "ipv6_string_to_raw");
    	}
    	info(log_fp, "# mcast addr: [%s]\n", global_mgid);
    }

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

#ifdef DEBUG    
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

void get_tailq_message()
{
    pthread_spin_lock(&tailq_lock);
    while (!TAILQ_EMPTY(&tailhead)) {
            tailq_entry_t* n3 = TAILQ_FIRST(&tailhead);
            SRV_DATA->last_write_csm_idx = log_append_entry(SRV_DATA->log, SID_GET_TERM(SRV_DATA->ctrl_data->sid), n3->req_id, n3->connection_id, n3->type, &n3->cmd);
            TAILQ_REMOVE(&tailhead, n3, entries);
            free(n3);
    }
    pthread_spin_unlock(&tailq_lock);
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

        /* Check the type of the operation */
        type = ud_hdr->type;
        if (MSG_NONE == prev_type) {
            prev_type = type;
        }
    }
    else {
        /* Rearm and try again */
        ud_post_one_receive(wc->wr_id);
        goto get_message;
    }

handle_messages:    
    /* Handle other messages */    
    if (wc_count > rd_wr_count) {
        /* There is one more message */
        ud_hdr = (ud_hdr_t*)
                (IBDEV->ud_recv_bufs[wc_array[wc_count-1].wr_id] + 40);
        if (IBV_SERVER == IBDEV->ulp_type) {
            type = handle_message_from_client(&wc_array[wc_count-1], ud_hdr);
        }
        /* Rearm receive operation */
        ud_post_one_receive(wc_array[wc_count-1].wr_id);
        return type;
    }    
    
    return type;
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
    request->gid = IBDEV->gid;
    request->type = JOIN;
    request->slid = get_unique_slid();

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
    dare_ep_t *ep = ep_search(&SRV_DATA->endpoints, request->slid);
    if (ep == NULL) {
        /* No ep with this LID; create a new one */
        ep = ep_insert(&SRV_DATA->endpoints, request->slid, request->gid);
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
    wc_to_ud_ep(&ib_ep->ud_ep, wc, request->gid, request->slid);
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
            request->slid, CONFIG, &SRV_DATA->config.cid);
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
    request->hdr.gid       = IBDEV->gid;
    request->hdr.slid      = get_unique_slid();
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
        wc_to_ud_ep(&ep->ud_ep, wc, msg->hdr.gid, msg->hdr.slid);
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
    reply->hdr.gid       = IBDEV->gid;
    reply->hdr.slid      = get_unique_slid();
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
        wc_to_ud_ep(&ep->ud_ep, wc, msg->hdr.gid, msg->hdr.slid);
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
    
    reconf_rep_t *psm_reply;
    uint32_t len;
    uint8_t i;
    
    /* Find the ep that send this request */
    dare_ep_t *ep = ep_search(&SRV_DATA->endpoints, lid);
    // TODO: you should get the last_req_id from the protocol SM
    ep->last_req_id = req_id;
    ep->committed = 1;
    
    /* Create reply */
    switch(type) {
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

#endif

/* ================================================================== */

static int
wc_to_ud_ep(ud_ep_t *ud_ep, struct ibv_wc *wc, union ibv_gid dest_gid, uint16_t slid)
{
    ud_ep->lid = slid;
    ud_ep->gid = dest_gid;
    /* Create new AH for this LID */
    if (NULL != ud_ep->ah) {
        /* AH already created - destroy to be on the safe side */
        ud_ah_destroy(ud_ep->ah);
    }
    ud_ep->ah = ud_ah_create(wc->slid, ud_ep->gid);
    if (NULL == ud_ep->ah) {
        error_return(1, log_fp, "Cannot create AH for LID %"PRIu16"\n", 
                     ud_ep->lid);
    }
    ud_ep->qpn = wc->src_qp;
    return 0;
}

static uint16_t
get_unique_slid()
{
    char name[65];
    gethostname(name, sizeof(name));
    uint16_t lid = name[21] - '0';
    return lid;
}
