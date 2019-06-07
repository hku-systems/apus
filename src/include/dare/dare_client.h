/**                                                                                                      
 * DARE (Direct Access REplication)
 * 
 * Client implementation
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#ifndef DARE_CLIENT_H
#define DARE_CLIENT_H 

#include <stdio.h>
#include <ev.h>
#include "../../../utils/rbtree/include/rbtree.h"
#include "./dare.h"
#include "./dare_sm.h"
#include "./timer.h"

/* Retransmission period in ms */ 
#ifdef DEBUG
#define CLT_RETRANS_PERIOD 500
#define CLT_OUTPUT_PERIOD 100
#else 
#define CLT_RETRANS_PERIOD 20
#define CLT_OUTPUT_PERIOD 10
#endif 

/* Client types */
#define CLT_TYPE_RECONF 1
#define CLT_TYPE_LOOP   2
#define CLT_TYPE_TRACE  3
#define CLT_TYPE_RTRACE  4

#define MAX_LINE_LENGTH 128

struct dare_client_input_t {
    FILE* log;
    char* trace;
    char* output;
    uint8_t clt_type;
    uint8_t sm_type;
    uint8_t first_op_perc;
    uint8_t group_size;
};
typedef struct dare_client_input_t dare_client_input_t;

struct dare_client_data_t {
    dare_client_input_t *input;
    struct ev_loop      *loop;   // loop for EV library
    void                *leader_ep;
    FILE                *trace_fp;
    FILE                *output_fp;
    dare_sm_t           *sm;        // local state machine
    HRT_TIMESTAMP_T     t1, t2;
};
typedef struct dare_client_data_t dare_client_data_t;

/* ================================================================== */

int dare_client_init( dare_client_input_t *input );
void dare_client_shutdown();

#endif /* DARE_CLIENT_H */
