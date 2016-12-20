#ifndef CONSENSUS_H

#define CONSENSUS_H
#include "../util/common-header.h"
#include "../rdma/dare_log.h"
#include "../output/output.h"

typedef uint64_t db_key_type;
struct node_t;
struct consensus_component_t;

typedef void (*user_cb)(db_key_type index,void* arg);
typedef int (*up_check)(void* arg);
typedef int (*up_get)(view_stamp clt_id, void* arg);

typedef enum con_role_t{
    LEADER = 0,
    SECONDARY = 1,
}con_role;

struct consensus_component_t* init_consensus_comp(struct node_t*,uint8_t,FILE*,int,int,
        const char*,void*,int,
        view*,view_stamp*,view_stamp*,view_stamp*,user_cb,up_check,up_get,void*);

dare_log_entry_t* leader_handle_submit_req(struct consensus_component_t*,size_t,void*,uint8_t,view_stamp*);

void *handle_accept_req(void* arg);

#endif
