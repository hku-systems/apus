#ifndef EV_MGR_H 
#define EV_MGR_H

#include "../util/common-header.h"
#include "../rsm-interface.h"
#include "../replica-sys/replica.h"
#include "uthash.h"

typedef uint8_t nid_t;

struct event_manager_t;

typedef struct leader_tcp_pair_t{
    int key;
    view_stamp vs;
    UT_hash_handle hh;
}leader_tcp_pair;

typedef struct leader_udp_pair_t{
    char sa_data[14];
    view_stamp vs;
    UT_hash_handle hh;
}leader_udp_pair;

typedef struct replica_tcp_pair_t{
    view_stamp key;
    int p_s;
    int s_p;
    int accepted;
    UT_hash_handle hh;
}replica_tcp_pair;

typedef struct mgr_address_t{
    struct sockaddr_in s_addr;
    size_t s_sock_len;
}mgr_address;

typedef struct event_manager_t{
    nid_t node_id;
    leader_tcp_pair* leader_tcp_map;
    replica_tcp_pair* replica_tcp_map;
    leader_udp_pair* leader_udp_map;
    mgr_address sys_addr;

    // log option
    int ts_log;
    int sys_log;
    int stat_log;
    int req_log;

    int check_output;
    int rsm;

    db_key_type cur_rec;

    list *excluded_fds;
    list *excluded_threads;

    struct node_t* con_node;

    FILE* req_log_file;
    FILE* sys_log_file;
    char* db_name;
    db* db_ptr;

}event_manager;

typedef enum mgr_action_t{
    P_TCP_CONNECT=1,
    P_SEND=2,
    P_CLOSE=3,
    P_OUTPUT=4,
    P_NOP=5,
    P_UDP_CONNECT=6,
}mgr_action;

typedef enum check_point_state_t{
    NO_DISCONNECTED=1,
    DISCONNECTED_REQUEST=2,
    DISCONNECTED_APPROVE=3,
}check_point_state;

// declare
int disconnct_inner();
int reconnect_inner();

#endif
