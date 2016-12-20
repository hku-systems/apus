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

#ifndef DARE_IBV_RC_H
#define DARE_IBV_RC_H

#include <infiniband/verbs.h> /* OFED stuff */
#include <dare_ibv.h>

#define SIGNALED    1
#define NOTSIGNALED 0
//#define NOTSIGNALED 1

/**
 * The WR Identifier (WRID)
 * the WRID is a 64-bit value [SSN|WA|TAG|CONN], where
    * SSN is the Send Sequence Number
    * WA is the Wrap-Around flag, set for log update WRs 
    * TAG is a flag set for special signaled WRs (to avoid QPs overflow)
    * CONN is a 8-bit index that identifies the connection (the remote server)
 */
/* The CONN consists of the 8 least significant bits (lsbs) */
#define WRID_GET_CONN(wrid) (uint8_t)((wrid) & (0xFF))
#define WRID_SET_CONN(wrid, conn) (wrid) = (conn | ((wrid >> 8) << 8))
/* The TAG flag is the 9th lsb */
#define WRID_GET_TAG(wrid) ((wrid) & (1 << 8))
#define WRID_SET_TAG(wrid) (wrid) |= 1 << 8
#define WRID_UNSET_TAG(wrid) (wrid) &= ~(1 << 8)
/* The WA flag is the 10th lsb */
#define WRID_GET_WA(wrid) ((wrid) & (1 << 9))
#define WRID_SET_WA(wrid) (wrid) |= 1 << 9
#define WRID_UNSET_WA(wrid) (wrid) &= ~(1 << 9)
/* The SSN consists of the most significant 54 bits */
#define WRID_GET_SSN(wrid) ((wrid) >> 10)
#define WRID_SET_SSN(wrid, ssn) (wrid) = (((ssn) << 10) | ((wrid) & 0x3FF))

#define PRINT_WRID(wrid) info(log_fp,     \
    " [%010"PRIu64"|%d|%d|%03"PRIu8"] ", \
    WRID_GET_SSN(wrid),  \
    (WRID_GET_WA(wrid) ? 1 : 0),   \
    (WRID_GET_TAG(wrid) ? 1 : 0),   \
    WRID_GET_CONN(wrid))
#define PRINT_WRID_(wrid) PRINT_WRID(wrid); info(log_fp, "\n");

int rc_init();
void rc_free();

/* Start up */
int rc_get_replicated_vote();
int rc_send_sm_request();
int rc_send_sm_reply( uint8_t idx, void *s, int reg_mem );
int rc_recover_sm( uint8_t idx );
int rc_recover_log();

/* HB mechanism */
int rc_send_hb();
int rc_send_hb_reply( uint8_t idx );

/* Leader election */
int rc_send_vote_request();
int rc_replicate_vote();
int rc_send_vote_ack();

/* Normal operation */
int rc_verify_leadership( int *leader );
int rc_write_remote_logs( int wait_for_commit );
int rc_get_remote_apply_offsets();

/* QP interface */
int rc_disconnect_server( uint8_t idx );
int rc_connect_server( uint8_t idx, int qp_id );
int rc_revoke_log_access();
int rc_restore_log_access();

/* LogGP */
double rc_get_loggp_params( uint32_t size, int type, int *poll_count, int write, int inline_flag );
double rc_loggp_prtt( int n, double delay, uint32_t size );
int rc_loggp_exit();
 
int rc_print_qp_state( void *data );
void rc_ib_send_msg();
#endif /* DARE_IBV_RC_H */
