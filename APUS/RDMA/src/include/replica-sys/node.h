#ifndef NODE_H
#define NODE_H
#include "../util/common-header.h"
#include "../consensus/consensus.h"
#include "../db/db-interface.h"
#include "./replica.h"

typedef struct peer_t{
	struct sockaddr_in* peer_address;
}peer;

typedef void (*user_cb)(db_key_type index,void* arg);
typedef int (*up_check)(void* arg);
typedef int (*up_get)(view_stamp clt_id,void* arg);

typedef struct node_t{
	node_id_t node_id;
	int stat_log;
	int sys_log;
	view cur_view;
	view_stamp highest_to_commit;
	view_stamp highest_committed;
	view_stamp highest_seen;
	//consensus component
	struct consensus_component_t* consensus_comp;
	// replica group
	uint8_t group_size;
	peer* peer_pool;

	//databse part
	char* db_name;
	db* db_ptr;
	FILE* sys_log_file;
	int zoo_port;

	pthread_t rep_thread;
}node;

#endif
