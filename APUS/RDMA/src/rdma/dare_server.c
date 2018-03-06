#include "../include/rdma/dare_ibv.h"
#include "../include/rdma/dare_server.h"
#include <ev.h>

FILE *log_fp;

/* server data */
dare_server_data_t data;

const double hb_period = 0.01;
const uint64_t elec_timeout_low = 10000;
const uint64_t elec_timeout_high = 30000;
double recomputed_hb_timeout;
int leader_failed;
int hb_timeout_flag;
uint64_t latest_hb_received;

/* ================================================================== */
/* libEV events */

/* A timer event for adjusting the timeout period */
ev_timer to_adjust_event;

/* A timer event for heartbeat mechanism */
ev_timer hb_event;

/* ================================================================== */

/* ================================================================== */
/* local function - prototypes */

static int
init_server_data();
static void
free_server_data();
static void
init_network_cb();
static void
start_election();
static double
hb_timeout();

/* ================================================================== */

/* ================================================================== */
/* Callbacks for libEV */

static void
hb_receive_cb( EV_P_ ev_timer *w, int revents );
static void
hb_send_cb( EV_P_ ev_timer *w, int revents );
static void
to_adjust_cb( EV_P_ ev_timer *w, int revents );

/* ================================================================== */


int dare_server_init(dare_server_input_t *input)
{   
    int rc;
    
    /* Initialize data fields to zero */
    memset(&data, 0, sizeof(dare_server_data_t));
    
    /* Store input into server's data structure */
    data.input = input;

    /* Set log file handler */
    log_fp = input->log;
    
    /* Init server data */    
    rc = init_server_data();
    if (0 != rc) {
        free_server_data();
        fprintf(stderr, "Cannot init server data\n");
        return 1;
    }

    init_network_cb();

    /* Init HB event */
    ev_timer_init(&hb_event, hb_receive_cb, 0., 0.);
    ev_set_priority(&hb_event, EV_MAXPRI-1);

    /* Init timeout adjust event */
    ev_timer_init(&to_adjust_event, to_adjust_cb, 0., 0.);
    ev_set_priority(&to_adjust_event, EV_MAXPRI-1);
    
    /* Now wait for events to arrive */
    ev_run(data.loop, 0);

    return 0;
}

void dare_server_shutdown()
{   
    dare_ib_srv_shutdown();
    free_server_data();
    fclose(log_fp);
    exit(1);
}

static int init_server_data()
{
    int i;

    data.config.idx = data.input->server_idx;
    data.config.len = MAX_SERVER_COUNT;
    data.config.cid.size[0] = data.input->group_size;
    data.config.cid.state   = CID_STABLE;
    for (i = 0; i < data.input->group_size; i++) {
        CID_SERVER_ADD(data.config.cid, i);
    } 
    data.config.servers = (server_t*)malloc(data.config.len * sizeof(server_t));
    if (NULL == data.config.servers) {
        error_return(1, log_fp, "Cannot allocate configuration array\n");
    }
    memset(data.config.servers, 0, data.config.len * sizeof(server_t));

    for (i = 0; i < MAX_SERVER_COUNT; i++) {
        data.config.servers[i].send_flag = 1;
    }

    for (i = 0; i < data.config.len; i++) {
        data.config.servers[i].peer_address = data.input->peer_pool[i].peer_address;
    }

    /* Set up log */
    data.log = log_new();
    if (NULL == data.log) {
        error_return(1, log_fp, "Cannot allocate log\n");
    }
    data.ctrl_data->sid = SID_NULL;
    
    return 0;
}

static void free_server_data()
{   
    log_free(data.log);
    
    if (NULL != data.config.servers) {
        free(data.config.servers);
        data.config.servers = NULL;
    }
}

static void init_network_cb()
{
    int rc; 
    
    /* Init IB device */
    rc = dare_init_ib_device();
    if (0 != rc) {
        rdma_error(log_fp, "Cannot init IB device\n");
        goto shutdown;
    }
    
    /* Init some IB data for the server */
    rc = dare_init_ib_srv_data(&data);
    if (0 != rc) {
        rdma_error(log_fp, "Cannot init IB SRV data\n");
        goto shutdown;
    }
    
    /* Init IB RC */
    rc = dare_init_ib_rc();
        if (0 != rc) {
        rdma_error(log_fp, "Cannot init IB RC\n");
        goto shutdown;
    }
    
    return;
    
shutdown:
    dare_server_shutdown();
}

/**
 * Periodically adjust the HB timeout
 */
static void
to_adjust_cb( EV_P_ ev_timer *w, int revents )
{
    static uint64_t total_count = 0;
    static uint64_t fp_count = 0;

    uint64_t hb;
    uint8_t leader = SID_GET_IDX(data.ctrl_data->sid);
    
    if (hb_timeout_flag) {
        w->repeat = 0;
        ev_timer_again(EV_A_ w);
        return;
    }

    /* Total number of trials */
    total_count++;
    
    /* Read HB and reset it - atomic operation */
    hb = __sync_fetch_and_and(&data.ctrl_data->hb[leader], 0);
    if (0 != hb) {
        /* HB received */
        latest_hb_received = hb;
        if (leader_failed) {
            /* False possitive */
            fp_count++;
            leader_failed = 0;
            /* Increament timer with 0.1 of HB period */
            recomputed_hb_timeout += hb_period * 1;
            // info_wtime(log_fp, "false possitive => increase recomputed timeout: %lf\n", recomputed_hb_timeout);
        }
    }
    else {
        /* No HB */
        leader_failed = 1;
    }

    if (!hb_timeout_flag) {
        if ( (total_count > 100000) && ((double)fp_count/total_count < 0.0001) ) {
            /* Less than 0.01% false possitives */
            // info_wtime(log_fp, "New timeout: %lf (old one was %lf)\n", recomputed_hb_timeout, hb_timeout());
            // info(log_fp, "   # %"PRIu64" fp out of %"PRIu64"\n", fp_count, total_count);
            hb_timeout_flag = 1;
            /* From now on the timeout period is adjusted during the HB */ 
            w->repeat = 0;
            ev_timer_again(EV_A_ w);
            return;
        }
    }
    
    /* Reset timer */
    w->repeat = recomputed_hb_timeout;
    ev_timer_again(EV_A_ w);
}

static void
hb_receive_cb( EV_P_ ev_timer *w, int revents )
{   
    int rc;
    int timeout = 1;
    uint64_t hb;
    uint64_t new_sid;
    uint8_t i, size;
    
    /* Cannot receive HBs from servers in the extended config */
    // size = get_group_size(data.config);
    size = data.config.cid.size[0];
    
    /* Be sure that the tail does not remain set from a previous leadership */
    data.log->tail = data.log->len;

    uint8_t leader = SID_GET_IDX(data.ctrl_data->sid);
    new_sid = data.ctrl_data->sid;
    for (i = 0; i < size; i++) {
        if ( (i == data.config.idx) || !CID_IS_SERVER_ON(data.config.cid, i) )
            continue;
        if (i == leader) {
            if (!latest_hb_received) {
                latest_hb_received = __sync_fetch_and_and(&data.ctrl_data->hb[leader], 0);
            }
            /* HBs from leader are checked while adjusting the timeout */
            hb = latest_hb_received;
            latest_hb_received = 0;
        }
        else {
            /* Read HB and then reset it */
            hb = __sync_fetch_and_and(&data.ctrl_data->hb[i], 0);
        }
        if (0 == hb) {
            /* No heartbeat */
            continue;
        }
        if (hb < new_sid) {
            if (SID_GET_L(hb)) {
                /* Received HB from outdated leader */
            }
            continue;
        }

        /* Somebody sent me an up-to-date HB */
        timeout = 0;    
        /* Check if it is from a leader */
        if (SID_GET_L(hb)) {
            /* The HB was from a leader */
            new_sid = hb;
        }
    }
//text(log_fp, "\n");    
    
    if (timeout) {
        w->repeat = 0.;
        ev_timer_again(EV_A_ w);
        start_election(); 
        return;
    }
 
    if (new_sid != data.ctrl_data->sid) {       
        /* This SID is better */
        return;
    }
    
rearm:    
    /* Rearm HB event */
    //w->repeat = random_election_timeout();
    w->repeat = hb_timeout();
    //info_wtime(log_fp, "RECV HB (next=%lf sec)\n", w->repeat);
    ev_timer_again(EV_A_ w);
}

/**
 * Periodically update HB flag
 */
static void
hb_send_cb( EV_P_ ev_timer *w, int revents )
{
    int rc;
    static ev_tstamp last_hb = 0;
    
    /* Check if any server sent an HB reply; if that's the case, we need 
     * to incorporate that server into the active servers; restart election */
    uint64_t new_sid = data.ctrl_data->sid;
    uint64_t hb;
    uint8_t i, size;
    
    /* No need to send HBs to servers in the extended config */
    // size = get_group_size(data.config);
    size = data.config.cid.size[0];
    for (i = 0; i < size; i++) {
        if ( (i == data.config.idx) || !CID_IS_SERVER_ON(data.config.cid, i) )
            continue;

        /* Read HB and then reset it */
        hb = __sync_fetch_and_and(&data.ctrl_data->hb[i], 0);
        if (hb < new_sid) continue;

        /* Somebody sent me an HB reply with a higher term */
        // info_wtime(log_fp, "Received HB from p%"PRIu8" with higher term %"PRIu64"\n", 
                    // i, SID_GET_TERM(hb));
        rc = server_update_sid(new_sid, data.ctrl_data->sid);
        if (0 != rc) {
            /* Cannot update SID */
            return;
        }
        // server_to_follower();
        return;
    }

    /* Send HB to all servers */
    rc = dare_ib_send_hb();
    if (0 != rc) {
        //error(log_fp, "Cannot send heartbeats\n");
        goto shutdown;
    }
    ev_tstamp err = 0;
    if (last_hb != 0) {
        err = ev_now(EV_A) - (last_hb + hb_period);
    }
    last_hb = ev_now(EV_A);
    static uint64_t errs = 0;
    static uint64_t total = 0; total++;
    //info_wtime(log_fp, "SEND HB (err=%lf) (last_hb=%lf)\n", err, last_hb);
    if (err > hb_period) {
        errs++;
        // info_wtime(log_fp, "SEND HB (err=%lf)\n", err);
        //info_wtime(log_fp, "TIME ERROR %"PRIu64" out of %"PRIu64"\n", errs, total);
        //dare_server_shutdown();
    }
    
    /* Rearm timer */
    w->repeat = hb_period;
    ev_timer_again(EV_A_ w);
    
    return;

shutdown:
    w->repeat = 0;
    ev_timer_again(EV_A_ w);
    // dare_server_shutdown();
}

/* ================================================================== */
/* Leader election */

/**
 * Generate random election timeout in seconds
 */
static double
random_election_timeout()
{
    /* Generate time in microseconds in given interval */
    struct timeval tv;
    gettimeofday(&tv,NULL);
    uint64_t seed = data.config.idx*((tv.tv_sec%100)*1e6+tv.tv_usec);
    srand48(seed);
    uint64_t timeout = (lrand48() % (elec_timeout_high-elec_timeout_low)) 
                        + elec_timeout_low;
//info(log_fp, "election to in sec: %lf\n", (double)timeout * 1e-6);
    /* Return time in seconds */
    return (double)timeout * 1e-6;
}

static double
hb_timeout()
{
    if (hb_timeout_flag) {
        return recomputed_hb_timeout;
    }
    return 10 * hb_period;
}

/**
 * Start election
 */
static void
start_election()
{
    int rc, i;
    
    /* Get the latest SID */
    uint64_t new_sid = 0;    
    
    /* Set SID to [t+1|0|own_idx] */
    SID_SET_TERM(new_sid, SID_GET_TERM(data.ctrl_data->sid) + 1);
    SID_UNSET_L(new_sid);                   // no leader :(
    SID_SET_IDX(new_sid, data.config.idx);  // I can be the leader :)
    rc = server_update_sid(new_sid, data.ctrl_data->sid);
    if (0 != rc) {
        return;
    }
 
    /* Revoke access to the local log; we need exclusive access */
    // rc = dare_ib_revoke_log_access();
    // if (0 != rc) {
        /* This should never happen */
        // error_exit(1, log_fp, "Cannot get exclusive access to local log\n");
    // }
   
    /**
     *  Become a candidate 
     */
    //uint8_t size = get_group_size(data.config);
    uint8_t size = data.config.cid.size[0];
    for (i = 0; i < size; i++) {
        /* Clear votes from a previous election */
        data.ctrl_data->vote_ack[i] = data.log->len;
        /* Set next step of the normal operation process */
        // data.config.servers[i].next_lr_step = LR_GET_WRITE;
        /* Set send flag */
        data.config.servers[i].send_flag = 1;
    }

    /* Restart HB mechanism in receive mode; cannot wait forever for 
    followers to respond to me */
    ev_set_cb(&hb_event, hb_receive_cb);
    hb_event.repeat = random_election_timeout();
    //debug(log_fp, "Schedule HB event in %lf sec\n", hb_event.repeat);
    ev_timer_again(data.loop, &hb_event); 
    
    /* Send vote requests */
    rc = dare_ib_send_vote_request();
    if (0 != rc) {
        /* This should never happen */
        //error_exit(1, log_fp, "Cannot send vote request\n");
    }
}

/**
 * Check the number of votes received
 */
static void 
poll_vote_count()
{
    int rc;
    uint8_t vote_count[2];
    vote_count[0] = 1;
    vote_count[1] = 1;
    // uint8_t i, size = get_group_size(data.config);
    uint8_t i, size = data.config.cid.size[0];
    uint64_t remote_commit;
    
    for (i = 0; i < size; i++) {
        if (i == data.config.idx) continue;
        remote_commit = data.ctrl_data->vote_ack[i];
        if (data.log->len == remote_commit) {
            /* No reply from this server */
            continue;
        }
        /* Count votes */
        if (i < data.config.cid.size[0]) {
            vote_count[0]++;
        }
        if (i < data.config.cid.size[1]) {
            vote_count[1]++;
        }
        
        /* Store the received commit offset */
        // data.ctrl_data->log_offsets[i].commit = remote_commit;
        /* No need to get the commit offset for this server */
        // data.config.servers[i].next_lr_step = LR_GET_NCE_LEN;
        // if (log_is_offset_larger(data.log, remote_commit, data.log->commit)) {
            /* Update local commit offset */
            // data.log->commit = remote_commit;
        // }
    }
        
    if (vote_count[0] <  data.config.cid.size[0] / 2 + 1) {
        return;
    }
    if (CID_STABLE != data.config.cid.state) {
        if (vote_count[1] <  data.config.cid.size[1] / 2 + 1) {
            return;
        }
    }
    info(log_fp, "Votes:");
    for (i = 0; i < size; i++) {
        if (i == data.config.idx) continue;
        remote_commit = data.ctrl_data->vote_ack[i];
        if (data.log->len != remote_commit) {
            info(log_fp, " (p%"PRIu8")", i);
        }
    }
    info(log_fp, "\n");
    
    /**
     *  Won election: become leader 
     */
    //debug(log_fp, "vote count = %"PRIu8"\n", vote_count[0]);
    
    /* Update own SID to [t|1|own_idx] */
    uint64_t new_sid = data.ctrl_data->sid;
    SID_SET_L(new_sid);
    rc = server_update_sid(new_sid, data.ctrl_data->sid);
    if (0 != rc) {
        return;
    }

become_leader:
    /* Start sending heartbeats */
    ev_set_cb(&hb_event, hb_send_cb);
    hb_event.repeat = NOW;
    ev_timer_again(data.loop, &hb_event);
    
    /* Suspend timeout adjusting mechanism */
    to_adjust_event.repeat = 0;
    ev_timer_again(data.loop, &to_adjust_event);
    
    /* Gain log access */
    // rc = dare_ib_restore_log_access();
    // if (0 != rc) {
        /* This should never happen */
        // error_exit(1, log_fp, "Cannot restore log access\n");
    // }
}

/**
 * Poll for vote requests from other servers
 * the vote request is a tuple (sid, idx, term)
 *  - first, check if there is a decent sid 
 *  - then, revoke access to the log and do the up-to-date test
 */
static void
poll_vote_requests()
{
    int rc;
    uint8_t i, size = data.config.cid.size[0];
    uint64_t new_sid;
    vote_req_t *request;
    
    /* To avoid crazy servers removing good leaders */
    if (SID_GET_L(data.ctrl_data->sid)) {
        return;
    }
    
    uint8_t possible_leader = SID_GET_IDX(data.ctrl_data->sid);
    uint64_t hb = data.ctrl_data->hb[possible_leader];
    if ( (0 != hb) && (SID_GET_TERM(hb) == SID_GET_TERM(data.ctrl_data->sid)) ) {
        /* My vote counts (democracy at its best)...  */
        server_update_sid(hb, data.ctrl_data->sid);
        return;
    }

    uint64_t old_sid = data.ctrl_data->sid; SID_SET_L(old_sid);
    uint64_t best_sid = old_sid;
    for (i = 0; i < size; i++) {
        if (i == data.config.idx) continue;
        request = &(data.ctrl_data->vote_req[i]);
        if (request->sid != 0) {
            // text(log_fp, "Vote request from:"); PRINT_SID_(request->sid); 
        }
        if (best_sid >= request->sid) {
            /* Candidate's SID is not good enough; drop request */
            request->sid = 0;
            continue;
        }
        /* Don't reset "request->sid" yet */
        best_sid = request->sid;
        /* Note: we iterate through the vote requests in idx order; thus, 
        other requests for the same term are also considered */
    }
    if (best_sid == old_sid) {
        /* No better SID */
        return;
    }
    
    /* I thought I saw a better candidate ... */
    uint64_t highest_term = SID_GET_TERM(best_sid);
    
    //~ info_wtime(log_fp, "Vote request from (p%"PRIu8", T%"PRIu64")\n", 
        //~ SID_GET_IDX(best_sid), SID_GET_TERM(best_sid));        
           
    /* Revoke remote access to local log; need to have exclusive 
     * access to the log to get the last entry */
    // rc = dare_ib_revoke_log_access();
    // if (0 != rc) {
        /* This should never happen */
        // error_exit(1, log_fp, "Cannot lock local log\n");
    // }
    
    /* Create not committed buffer & get best request */
    vote_req_t best_request;
    best_request.sid = old_sid;
     
    /* Choose the best candidate */
    for (i = 0; i < size; i++) {
        request = &(data.ctrl_data->vote_req[i]);
        if (best_request.sid > request->sid) {
            /* Candidate's SID is not good enough; drop request */
            request->sid = 0;
            continue;
        }
        if (highest_term < SID_GET_TERM(request->sid))
            highest_term = SID_GET_TERM(request->sid);
        info(log_fp, "   # Remote(p%"PRIu8") [idx=%"PRIu64"; term=%"PRIu64"]\n", 
                    i, request->index, request->term);
        if ( (best_request.term > request->term) || 
             ((best_request.term == request->term) && 
              (best_request.index > request->index)) )
        {
            /* Candidate's log is not good enough; drop request */
            request->sid = 0;
            continue;
        }
        /* I like this candidate */
        best_request.index = request->index;
        best_request.term = request->term;
        best_request.sid = request->sid;
        best_request.cid = request->cid;
        request->sid = 0;
    }

    if (best_request.sid == old_sid) {
        /* Local log is better than remote logs; yet, local term is too low. 
        Increase TERM to increase chances to win election */
        new_sid = data.ctrl_data->sid;
        SID_SET_TERM(new_sid, highest_term);
        SID_SET_IDX(new_sid, data.config.idx);  // don't vote for anyone
        rc = server_update_sid(new_sid, data.ctrl_data->sid);
        if (0 != rc) {
            /* Could not update my SID; just return */
            return;
        }
        return;
    }
     
    /* Stop HB mechanism for the moment ... */
    hb_event.repeat = 0.;
    ev_timer_again(data.loop, &hb_event); 
    
    /* Update my local SID to show support */
    rc = server_update_sid(best_request.sid, data.ctrl_data->sid);
    if (0 != rc) {
        /* Could not update my SID; just return */
        return;
    }       

    /* Update configuration according to the candidate */
    // update_cid(best_request.cid);
            
    /* Replicate this SID in case I crash */
    // rc = dare_ib_replicate_vote();
    // if (0 != rc) {
    //     This should never happen 
    //    error_exit(1, log_fp, "Cannot replicate votes\n");
    // }
    
    /* Stop log pruning */
    // prune_event.repeat = 0.;
    // ev_timer_again(data.loop, &prune_event);

    leader_failed = 0;
    if (!hb_timeout_flag) {
        /* Restart timeout adjusting mechanism */
        to_adjust_event.repeat = recomputed_hb_timeout;
        ev_timer_again(data.loop, &to_adjust_event);
    }
    
    /* Restore access to the log (for the supported candidate) */
    // rc = dare_ib_restore_log_access();
    // if (0 != rc) {
        /* This should never happen */
        // error_exit(1, log_fp, "Cannot restore log access\n");
    // }
    
    /* Send ACK to the candidate */
    rc = dare_ib_send_vote_ack();
    if (rc < 0) {
        /* Operation failed; start an election */
        start_election(); 
        return;
    }
    if (0 != rc) {
        /* This should never happen */
        // error_exit(1, log_fp, "Cannot send vote ack\n");
    }

    /* Restart HB mechanism in receive mode */
    ev_set_cb(&hb_event, hb_receive_cb);
    // TODO: this should be a different timeout
    //double tmp = random_election_timeout(); 
    double tmp = hb_timeout();
    //hb_event.repeat = random_election_timeout();
    hb_event.repeat = tmp;
    ev_timer_again(data.loop, &hb_event);   
}

int server_update_sid( uint64_t new_sid, uint64_t old_sid )
{
    int rc;
    rc = __sync_bool_compare_and_swap(&(data.ctrl_data->sid),
                                    old_sid, new_sid);
    if (!rc) {
        //error_exit(1, log_fp, "CAS failed\n");
    }
    return 0;
}
