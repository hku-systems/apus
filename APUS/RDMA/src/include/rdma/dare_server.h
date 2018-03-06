#ifndef DARE_SERVER_H
#define DARE_SERVER_H 

#include "dare_log.h"
#include "dare.h"
#include "../replica-sys/node.h"

#define SID_GET_IDX(sid) (uint8_t)((sid) & (0xFF))
#define SID_SET_IDX(sid, idx) (sid) = (idx | ((sid >> 8) << 8))
/* The L flag is the 9th lsb */
#define SID_GET_L(sid) ((sid) & (1 << 8))
#define SID_SET_L(sid) (sid) |= 1 << 8
#define SID_UNSET_L(sid) (sid) &= ~(1 << 8)
/* The TERM consists of the most significant 55 bits */
#define SID_GET_TERM(sid) ((sid) >> 9)
#define SID_SET_TERM(sid, term) (sid) = (((term) << 9) | ((sid) & 0x1FF))

#define SID_NULL 0xFF

struct server_t {
    void *ep;               // endpoint data (network related)
    struct sockaddr_in* peer_address;
    uint8_t send_flag;
};
typedef struct server_t server_t;

struct vote_req_t {
    uint64_t sid;
    uint64_t index;
    uint64_t term;
    dare_cid_t cid;
};
typedef struct vote_req_t vote_req_t;

struct dare_server_input_t {
    FILE *log;
    peer *peer_pool;
    uint8_t group_size;
    uint8_t server_idx;
};
typedef struct dare_server_input_t dare_server_input_t;

struct ctrl_data_t {
    /* State identified (SID) */
    uint64_t    sid;
    
    /* DARE arrays */
    vote_req_t    vote_req[MAX_SERVER_COUNT];       /* vote requests */
    uint64_t 	  hb[MAX_SERVER_COUNT];             /* heartbeat array */ 
    uint64_t      vote_ack[MAX_SERVER_COUNT];
};
typedef struct ctrl_data_t ctrl_data_t;

struct dare_server_data_t {
    dare_server_input_t *input;
    
    server_config_t config; // configuration 
    ctrl_data_t *ctrl_data;  // control data (state & private data)
    dare_log_t  *log;       // local log (remotely accessible)

    struct ev_loop *loop;   // loop for EV library
};
typedef struct dare_server_data_t dare_server_data_t;

/* ================================================================== */

int dare_server_init( dare_server_input_t *input );

int server_update_sid( uint64_t new_sid, uint64_t old_sid );

#endif /* DARE_SERVER_H */
