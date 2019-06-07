#ifndef PROXY_H 
#define PROXY_H

#include "../util/common-header.h"
#include "../rsm-interface.h"
#include "../../../utils/uthash/uthash.h"
#include "../db/db-interface.h"
#include <sys/queue.h>

#define CONNECT 4
#define SEND    5
#define CLOSE   6

typedef uint16_t hk_t;
typedef uint8_t nc_t;
typedef uint8_t nid_t;

struct list_entry_t {
    pthread_t tid;
    LIST_ENTRY(list_entry_t) entries;
};
typedef struct list_entry_t list_entry_t;

LIST_HEAD(, list_entry_t) listhead;

typedef struct proxy_address_t{
    struct sockaddr_in s_addr;
    size_t s_sock_len;
}proxy_address;

typedef struct socket_pair_t{
    int clt_id;
    uint64_t req_id;
    uint16_t connection_id;
    int p_s;
    
    UT_hash_handle hh;
}socket_pair;

typedef struct proxy_node_t{
	proxy_address sys_addr;

    socket_pair* leader_hash_map;
    socket_pair* follower_hash_map;
    uint64_t highest_rec;
    uint64_t cur_rec;
    nc_t pair_count;
	
    // log option
    int req_log;

	FILE* req_log_file;
	char* db_name;
	db* db_ptr;
}proxy_node;

typedef struct proxy_msg_header_t{
    uint16_t connection_id;
    uint8_t action;
}proxy_msg_header;
#define PROXY_MSG_HEADER_SIZE (sizeof(proxy_msg_header))

typedef struct proxy_connect_msg_t{
    proxy_msg_header header;
}proxy_connect_msg;
#define PROXY_CONNECT_MSG_SIZE (sizeof(proxy_connect_msg))

struct fake_dare_cid_t {
    uint64_t epoch;
    uint8_t size[2];
    uint8_t state;
    uint8_t pad[1];
    uint32_t bitmask;
};
typedef struct fake_dare_cid_t fake_dare_cid_t;

struct fake_sm_cmd_t {
    uint16_t    len;
    uint8_t cmd[0];
};
typedef struct fake_sm_cmd_t fake_sm_cmd_t;

typedef struct proxy_send_msg_t{
    proxy_msg_header header;
    union {
        fake_sm_cmd_t   cmd;
        fake_dare_cid_t cid;
        uint64_t head;
    } data;
}proxy_send_msg;
#define PROXY_SEND_MSG_SIZE(M) (M->data.cmd.len+sizeof(proxy_send_msg))

typedef struct proxy_close_msg_t{
    proxy_msg_header header;
}proxy_close_msg;
#define PROXY_CLOSE_MSG_SIZE (sizeof(proxy_close_msg))

#endif