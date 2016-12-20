#ifndef REPLICA_H
#define REPLICA_H

#include "../db/db-interface.h"
#include "../rdma/dare_log.h"

typedef struct request_record_t{
    size_t data_size;
    uint8_t type;
    view_stamp clt_id;
    char data[0];
}request_record;
#define REQ_RECORD_SIZE(M) (sizeof(request_record)+(M->data_size))
typedef uint64_t db_key_type;

struct node_t;

struct node_t* system_initialize(uint8_t node_id,const char* config_path,const char* log_path,void(*user_cb)(db_key_type index,void* arg),int(*up_check)(void* arg),int(*up_get)(view_stamp clt_id, void* arg),void* db_ptr,void* arg);

dare_log_entry_t* rsm_op(struct node_t* my_node, size_t ret, void *buf, uint8_t type, view_stamp* clt_id);

uint32_t get_leader_id(struct node_t* my_node);
uint32_t get_group_size(struct node_t* my_node);
int disconnect_zookeeper();
int launch_replica_thread(struct node_t* my_node, list*excluded_fds, list*excluded_threads);

#endif
