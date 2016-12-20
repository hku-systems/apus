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
 
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/types.h>
#include <signal.h>
#include <math.h>

#include <dare_ibv.h>
#include <dare_client.h>
#include <dare_kvs_sm.h>

#define MEASURE_COUNT 1000
//#define MEASURE_COUNT 3
unsigned long long g_timerfreq;
uint64_t ticks[MEASURE_COUNT];
double usecs; 

FILE *log_fp;
int terminate;

dare_client_data_t data;

/* Number of requests successfully completed */
uint64_t request_count;
uint64_t output_time;
uint64_t last_request_count;

/* Variables for reading the entire trace */
int locally_applied;
long int current_trace_fp;
int measure_count;
int repeat_last_cmd;
int loop_first_req_done;

long int first_op_fp;
long int second_op_fp;
ev_tstamp last_srand;

/* ================================================================== */
/* libEV events */

/* An idle event that acts as the main function... */
ev_idle main_event;

/* A timer event used for initialization and other stuff ... */
ev_timer timer_event;

/* A timer event used to periodically output the number of request ... */
ev_timer output_event;

/* ================================================================== */
/* local function - prototypes */

static int
init_client_data();
static void
free_client_data();

static void
poll_ud();
static int 
cmpfunc_uint64( const void *a, const void *b );
static void int_handler(int);

/* ================================================================== */
/* Callbacks for libEV */

static void
init_network_cb( EV_P_ ev_timer *w, int revents );
static void
get_first_trace_cmd_cb( EV_P_ ev_timer *w, int revents );
static void
output_cb( EV_P_ ev_timer *w, int revents );
static void
consume_trace_cb( EV_P_ ev_timer *w, int revents );
static void
resend_request_cb( EV_P_ ev_timer *w, int revents );
static void
main_cb( EV_P_ ev_idle *w, int revents );

/* ================================================================== */
/* Init and cleaning up */
#if 1

//int dare_client_init( FILE* log, int type, char* trace_file_name )
int dare_client_init( dare_client_input_t *input )
{   
    int rc;
    
    /* Store input into client's data structure */
    data.input = input;
    
    /* Set log file handler */
    log_fp = input->log;
    
    /* Set handler for SIGINT */
    signal(SIGINT, int_handler);
    
    /* Init client data */    
    rc = init_client_data();
    if (0 != rc) {
        free_client_data();
        error_return(1, log_fp, "Cannot init client data\n");
    }
    
    /* Init EV loop */
    data.loop = EV_DEFAULT;
    
    if (CLT_TYPE_RTRACE == data.input->clt_type) {
        /* Initialize timer */
        HRT_INIT(g_timerfreq);
    }
    
    /* Schedule timer event */
    ev_timer_init(&timer_event, init_network_cb, 0., NOW);
    ev_set_priority(&timer_event, EV_MAXPRI-1);
    ev_timer_again(data.loop, &timer_event);
    
    /* Schedule counter event */
    ev_timer_init(&output_event, output_cb, 0., 0.);
    ev_set_priority(&output_event, EV_MAXPRI-1);
    
    /* Init the poll event */
    ev_idle_init(&main_event, main_cb);
    ev_set_priority(&main_event, EV_MAXPRI);
    
    /* Now wait for events to arrive */
    ev_run(data.loop, 0);

    return 0;
}

void dare_client_shutdown()
{
    ev_timer_stop(data.loop, &timer_event);
    ev_timer_stop(data.loop, &output_event);
    ev_break(data.loop, EVBREAK_ALL);
    
    dare_ib_clt_shutdown();
    free_client_data();
    fclose(log_fp);
    exit(1);
}

static int
init_client_data()
{   
    /* Create local SM */
    data.sm = NULL;
    switch(data.input->sm_type) {
        case CLT_NULL:
            // TODO
            break;
        case CLT_KVS:
            data.sm = create_kvs_sm(0);
            break;
        case CLT_FS:
            // TODO
            break;
    }
    
    /* Open trace file */
    if (strcmp(data.input->trace, "") != 0) {
        data.trace_fp = fopen(data.input->trace, "rb");
        if (NULL == data.trace_fp) {
            error_return(1, log_fp, "Cannot open trace file\n");
        }
    }
        
    /* Open output file */
    if (strcmp(data.input->output, "") != 0) {
        data.output_fp = fopen(data.input->output, "w");
        if (NULL == data.output_fp) {
            error_return(1, log_fp, "Cannot open output file\n");
        }
    }

    /* No leader at the moment */
    data.leader_ep = NULL;
    
    return 0;
}

static void
free_client_data()
{
    if (NULL != data.sm) {
        data.sm->destroy(data.sm);
        data.sm = NULL;
    }
    
    if (NULL != data.trace_fp) {
        fclose(data.trace_fp);
        data.trace_fp = NULL;
    }
    
    if (NULL != data.output_fp) {
        fclose(data.output_fp);
        data.output_fp = NULL;
    }
    
    if (NULL != data.leader_ep) {
        free(data.leader_ep);
        data.leader_ep = NULL;
    }
}

#endif 

/* ================================================================== */
/* Starting up */
#if 1

/**
 * Initialize networking data 
 *  IB device; UD data; RC data; and start UD
 */
static void
init_network_cb( EV_P_ ev_timer *w, int revents )
{
    int rc; 
    
    /* Stop timer */
    w->repeat = 0;
    ev_timer_again(EV_A_ w);
    
    /* Init IB device: ibv_device; ibv_context & ud_init */
    rc = dare_init_ib_device(MAX_SERVER_COUNT);
    if (0 != rc) {
        error(log_fp, "Cannot init IB device\n");
        goto shutdown;
    }
    
    /* Init some IB data for the client */
    rc = dare_init_ib_clt_data(&data);
    if (0 != rc) {
        error(log_fp, "Cannot init IB CLT data\n");
        goto shutdown;
    }
        
    /* Start IB UD */
    rc = dare_start_ib_ud();
    if (0 != rc) {
        error(log_fp, "Cannot start IB UD\n");
        goto shutdown;
    }
    if (CLT_TYPE_RECONF == data.input->clt_type) {
        /* Create downsize request */
        dare_ib_create_clt_downsize_request();
        /* Schedule timer event to resend request */
        ev_set_cb(w, resend_request_cb);
        w->repeat = CLT_RETRANS_PERIOD / 1000.;
        ev_timer_again(EV_A_ w);
    }
    else if (CLT_TYPE_LOOP == data.input->clt_type) {
        /* Set file pointers for 1st and 2nd requests */
        first_op_fp = 0;
        // type
        uint8_t type;
        fread(&type, 1, 1, data.trace_fp);
        if (DOWNSIZE == type) {
            error(log_fp, "LOOP Client & downsize request...\n");
            goto shutdown;
        }
        // sizeof(kvs_cmd_t)
        kvs_cmd_t kvs_cmd;
        fread(&kvs_cmd, sizeof(kvs_cmd_t), 1, data.trace_fp);
        second_op_fp = kvs_cmd.len + sizeof(kvs_cmd_t) + 1;
        //info_wtime(log_fp, "%ld,%ld\n", first_op_fp, second_op_fp);
        /* Reset file pointer */
        rc = fseek(data.trace_fp, first_op_fp, SEEK_SET);
        srand (time(NULL));
        last_srand = ev_now(EV_A);
        
        /* Read first entry in the trace and create request */
        ev_set_cb(w, get_first_trace_cmd_cb);
        w->repeat = NOW;
        ev_timer_again(EV_A_ w);
        /* Schedule output event to periodically output the number of requests */
        ev_set_cb(&output_event, output_cb);
        output_event.repeat = CLT_OUTPUT_PERIOD / 1000.;
        while((int)(ev_now(EV_A) * 1000000) % CLT_OUTPUT_PERIOD == 0);
        ev_timer_again(EV_A_ &output_event);
    }
    else if ( (CLT_TYPE_RTRACE == data.input->clt_type) || 
                (CLT_TYPE_TRACE == data.input->clt_type) )
    {
        /* Schedule event to read requests*/
        ev_set_cb(w, consume_trace_cb);
        w->repeat = NOW;
        ev_timer_again(EV_A_ w);
    }
    
    /* Start poll event */   
    ev_idle_start(EV_A_ &main_event);
   
    return;

shutdown:
    dare_client_shutdown();
}

#endif 

/* ================================================================== */
/* Handle trace & sending requests */
#if 1

/**
 * Read first entry in the trace and create request
 */
static void
get_first_trace_cmd_cb( EV_P_ ev_timer *w, int revents )
{
    int rc;
    uint8_t n;
    static uint64_t first = 0, second = 0;
    static uint8_t percentage = 0;
    
    if (100 == data.input->first_op_perc) {
        rc = fseek(data.trace_fp, 0, SEEK_SET);
    }
    else {
        if (ev_now(EV_A) - last_srand >= 1) {
            last_srand = ev_now(EV_A);
            srand (time(NULL));
            //percentage += 10;
            //if (percentage > 100) dare_client_shutdown();
        }
        n = rand() % 100 + 1;
        if (n <= data.input->first_op_perc) {
        //if (n <= percentage) {
            first++;
            rc = fseek(data.trace_fp, first_op_fp, SEEK_SET);
        }
        else {
            second++;
            rc = fseek(data.trace_fp, second_op_fp, SEEK_SET);
        }
    }
    if (0 != rc) {
        error(log_fp, "Cannot reposition file pointer\n");
        dare_client_shutdown();
    }
      
    /* Get first command in the trace */
    rc = dare_ib_create_clt_request();
    if (rc < 0) {
        /* Trace is empty */
        error(log_fp, "The trace is empty\n");
        dare_client_shutdown();
    }
        
    /* Schedule timer event to resend request */
    ev_set_cb(w, resend_request_cb);
    w->repeat = CLT_RETRANS_PERIOD / 1000.;
    ev_timer_again(EV_A_ w);
}

static void
output_cb( EV_P_ ev_timer *w, int revents )
{
    if (last_request_count * 0.9 > request_count) {
        info_wtime(log_fp, "SPIKE (t=%"PRIu64"): %"PRIu64"->%"PRIu64"\n", 
                output_time, last_request_count, request_count);
    }
    output_time += CLT_OUTPUT_PERIOD;
    fprintf(data.output_fp, "output time: %"PRIu64"ms, request count: %"PRIu64", %lf\n", output_time, request_count, ev_now(EV_A));
    last_request_count = request_count;
    request_count = 0;
}

/**
 * Read a command from the trace file and send it to the RSM
 */
static void
consume_trace_cb( EV_P_ ev_timer *w, int revents )
{
    int rc, i;
    
    /* Stop timer */
    w->repeat = 0;
    ev_timer_again(EV_A_ w);
#if 0    
    /* First apply all the commands to the local SM - for comparison */
    while (!locally_applied) {
        current_trace_fp = ftell(data.trace_fp);
//debug(log_fp, "ctr_fp = %lu\n", current_trace_fp);        
        for (i = 0; i < MEASURE_COUNT; i++) {
            rc = fseek(data.trace_fp, current_trace_fp, SEEK_SET);
            if (0 != rc) {
                error_exit(1, log_fp, "Cannot reposition file pointer\n");
            }
            HRT_GET_TIMESTAMP(data.t1);
            rc = dare_ib_apply_cmd_locally();
            if (rc < 0) {
                /* Trace is empty */
                info(log_fp, "All CMDs were applied to the local SM\n\n");
                locally_applied = 1;
                rc = fseek(data.trace_fp, 0, SEEK_SET);
                if (0 != rc) {
                    error_exit(1, log_fp, "Cannot reposition file pointer\n");
                }
                current_trace_fp = 0;
                goto rsm;
            }
            if (rc != 0) {
                error_exit(1, log_fp, "Cannot apply SM CMD\n");
            }
            HRT_GET_TIMESTAMP(data.t2);
            HRT_GET_ELAPSED_TICKS(data.t1, data.t2, &(ticks[i]));
//info(log_fp, "%"PRIu64" ", ticks[i]);        
        }
//info(log_fp, "\n");        
        qsort(ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
        int median_index = MEASURE_COUNT/2;
        usecs = HRT_GET_USEC(ticks[median_index]);
        info(log_fp, "%9.3lf\n", usecs); 
    }


rsm:
#endif
    if (CLT_TYPE_TRACE == data.input->clt_type) {
        goto create_request;
    }
    else if (CLT_TYPE_RTRACE == data.input->clt_type) {
        goto repeat_trace;
    }

repeat_trace:    
    /* Then apply all the commands to the RSM */
    if (measure_count == MEASURE_COUNT) {
        /* First print the latency of this command */
        qsort(ticks, MEASURE_COUNT, sizeof(uint64_t), cmpfunc_uint64);
        for (i = 0; i < MEASURE_COUNT; i++) {
            fprintf(data.output_fp, "%9.3lf ", HRT_GET_USEC(ticks[i]));
        }
        fprintf(data.output_fp, "\n");
        /* How to get the median */
        //int median_index = MEASURE_COUNT/2;
        //usecs = HRT_GET_USEC(ticks[median_index]);
        //info(log_fp, "%9.3lf\n", usecs); 
        
        /* Then go to next CMD */
        measure_count = 0;
        current_trace_fp = ftell(data.trace_fp);
    }
    else {
        /* Repeat last command */
        rc = fseek(data.trace_fp, current_trace_fp, SEEK_SET);
        if (0 != rc) {
            error_exit(1, log_fp, "Cannot reposition file pointer\n");
        }
    }
    //if (!repeat_last_cmd) {
    //    HRT_GET_TIMESTAMP(data.t1);
    //}
create_request:    
    rc = dare_ib_create_clt_request();
    if (rc < 0) {
        /* Trace is empty */
        info(log_fp, "I finished my trace; bye bye\n");
        dare_client_shutdown();
    }
    if (rc != 0) {
        error(log_fp, "Cannot create CLT request\n");
    }
    
    /* Schedule timer event to resend request */
    ev_set_cb(w, resend_request_cb);
    w->repeat = CLT_RETRANS_PERIOD / 1000.;
    ev_timer_again(EV_A_ w);

    repeat_last_cmd = 1;
}

/**
 * Resend last request
 */
static void
resend_request_cb( EV_P_ ev_timer *w, int revents )
{
    int rc;
    rc = dare_ib_resend_clt_request();
    if (rc != 0) {
        error(log_fp, "Cannot resend request\n");
        dare_client_shutdown();
    }
}

#endif 

/* ================================================================== */
/* Idle event - main function */
#if 1

/**
 * Main callback
 */
static void
main_cb( EV_P_ ev_idle *w, int revents )
{
    if (terminate) {
        dare_client_shutdown();
    }

    /* Poll UD connection for incoming messages */
    poll_ud();
}

/**
 * Poll for UD messages
 */
static void
poll_ud()
{
    uint8_t type = dare_ib_poll_ud_queue();
    if (MSG_ERROR == type) {
        error(log_fp, "Cannot get UD message\n");
        dare_client_shutdown();
    }
    switch(type) {
        case CFG_REPLY:
            if (CLT_TYPE_RECONF == data.input->clt_type) {
                /* Received Reply from server - I'm done */
                dare_client_shutdown();
            }
        case CSM_REPLY:
            if (CLT_TYPE_LOOP == data.input->clt_type) {
                //if (loop_first_req_done) {
                    /* Increase request counter */
                    request_count++;
                //}
                //else {
                //    loop_first_req_done = 1;
                //}
                /* Reschedule the event to read first command */
                ev_set_cb(&timer_event, get_first_trace_cmd_cb);
                timer_event.repeat = NOW;
                ev_timer_again(data.loop, &timer_event);
                break;
            }
            if (CLT_TYPE_RTRACE == data.input->clt_type) {
                HRT_GET_TIMESTAMP(data.t2);
                HRT_GET_ELAPSED_TICKS(data.t1, data.t2, &(ticks[measure_count]));
                measure_count++;
                repeat_last_cmd = 0;
                /* Reschedule timer event to read next command */
                ev_set_cb(&timer_event, consume_trace_cb);
                timer_event.repeat = NOW;
                ev_timer_again(data.loop, &timer_event);
                break;
            }
            if (CLT_TYPE_TRACE == data.input->clt_type) {
                /* Reschedule timer event to read next command */
                ev_set_cb(&timer_event, consume_trace_cb);
                timer_event.repeat = NOW;
                ev_timer_again(data.loop, &timer_event);
                break;
            }
        default:
            break;
    }
}

#endif 

/* ================================================================== */
/* Others */
#if 1

static int 
cmpfunc_uint64( const void *a, const void *b )
{
    return ( *(uint64_t*)a - *(uint64_t*)b );
}

static void 
int_handler(int dummy) 
{
    fprintf(log_fp,"SIGINT detected; shutdown\n");
    //dare_server_shutdown();
    terminate = 1;
}

#endif 
