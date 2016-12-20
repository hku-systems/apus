/*
Author: Jingyu
Date: 	April 5, 2016
Description: The function do_decision will consider the hashvalues from all nodes, then make a decision about whether to launch a restore process.
*/

#include "../include/output/output.h"
#include "../include/util/debug.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define GUARD_SOCK "/tmp/guard.sock"
#define MAX_CMD_SIZE 512
//This function will count the number of nodes whose hashvalue is the same as aim_hash
int count_hash(output_peer_t* output_peers, int group_size, uint64_t aim_hash){
	int i=0;
	int cnt=0;
	for ( i=0;i<group_size;i++){
		if (output_peers[i].hash == aim_hash){
			cnt++;
		}
	}	
	return cnt;
}

/*
This function will return the maximum number of a sub-set whose hash value are the same.
For example, if the group is (1,1,1,2,2), the function will return 3. And
(1,2,3,4,5) => 1
(1,1,2,2,3) => 2 
*/
int major_count_hash(output_peer_t* output_peers, int group_size, uint64_t* hash_ptr){
	int i=0;
	int ret=0;
	int max_cnt=0;
	uint64_t aim_hash=0;
	for ( i=0;i<group_size;i++){
		aim_hash = output_peers[i].hash;
		ret = count_hash(output_peers, group_size, aim_hash);	
		if (ret>max_cnt){
			max_cnt = ret;
			*hash_ptr = aim_hash;
		}
	}
	return max_cnt;
}

uint64_t get_master_hash(output_peer_t* output_peers, int group_size){
	uint64_t master_hash=0;
	for (int i=0; i<group_size; i++){
		if (output_peers[i].leader_id == output_peers[i].node_id){
			master_hash = output_peers[i].hash;
			return master_hash;
		}
	}

	return master_hash;
}

void* call_send_restore_cmd_start(void* argv){
	output_peer_t *para = (output_peer_t*)argv;
	char cmd[MAX_CMD_SIZE];
	do{
		if (NULL == para){
			debug_log("[call_send_restore_cmd_start] parameters are invalid.\n");
			break;
		}
		//GUARD_SOCK
		// check file existed.
		if (0!=access(GUARD_SOCK,F_OK)){
			debug_log("[call_send_restore_cmd_start] sock %s is not existed. Check guard.py.\n",GUARD_SOCK);
			break;		
		}
		int fd,rc;
		if ( (fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
	    		debug_log("[call_send_restore_cmd_start] unix socket create error.\n");
	    		break;
		}
		struct sockaddr_un addr;
		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		strcpy(addr.sun_path, GUARD_SOCK);
		// It maybe blocked.
		if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
			debug_log("[call_send_restore_cmd_start] unix socket create error.\n");
			break;
		}
		sprintf(cmd,"restore %u %ld\n",(unsigned int)para->node_id,para->hash_index);
		rc = write(fd,cmd,strlen(cmd));
		if (-1 == rc){ // write error
			debug_log("[call_send_restore_cmd_start] write error: %d, cmd:%s\n",errno,cmd);
			break;
		}
	}while(0);
	// release parameter;
	free(argv);
	return NULL;
}
// will send restore cmd to the guard.py
int send_restore_cmd(uint32_t node_id,long hash_index){
	debug_log("[send_restore_cmd] id:%u, hash_index:%ld\n",node_id, hash_index);
	// It is better to start a thread to send the cmd, so that this thread will not be blocked.
	output_peer_t *para = (output_peer_t*)malloc(sizeof(output_peer_t));
	para->node_id = node_id;
	para->hash_index = hash_index;
	pthread_t thread_id;
	int ret=pthread_create(&thread_id,NULL,&call_send_restore_cmd_start,(void*)para);
	if (ret){ // On success, pthread_create() returns 0
    	debug_log("[send_restore_cmd] call call_send_restore_cmd_start() in a new thread failed. err:%d\n", ret);	
    	return -1;
    }
	return 0;    
}

//This function will send restore cmd to guard.py
// Any node's hash is different with aim_hash will be restored.
// If aim_hash is 0 , will nodes will be restored.
int do_restore(output_peer_t* output_peers, int group_size, uint64_t aim_hash){
	if (NULL == output_peers || 0 == group_size){
		debug_log("[do_restore] invalid parameters.\n");
		return -1;
	}
	for (int i=0; i< group_size; i++){
		debug_log("[do_restore] leader_id:%u, node_id: %u, hashval: 0x%"PRIx64" hash_index:%ld, aim_hash: 0x%"PRIx64"\n",
			output_peers[i].leader_id,
			output_peers[i].node_id,
			output_peers[i].hash,
			output_peers[i].hash_index,
			aim_hash);
		if (0==aim_hash || output_peers[i].hash != aim_hash){
			debug_log("[do_restore] node_id: %u, will be restored.\n",output_peers[i].node_id);
			send_restore_cmd(output_peers[i].node_id,output_peers[i].hash_index);
		}		
	}
	return 0;
}
// do_decision will open a file to log the decision
// make dicision and trigger a restore cmd to guard.py
int do_decision(output_peer_t* output_peers, int group_size){
	if (NULL == output_peers || 0 == group_size){
		debug_log("[do_decision] invalid parameters.\n");
		return -1;
	}
	static FILE * fp=NULL; 
	char f_name[64];
	if (NULL==fp){
		sprintf(f_name,"do_decision.%d.log",getpid());
		fp = fopen (f_name, "wb");
		if (NULL==fp){
			perror("[do_decision] fatal error, can not create do_decision log file.\n");
			return -1;
		}
	}
	int i=0;
	int threshold = 0; // threshold for majority
	int con_num = 0; // number of nodes whose hashvalue is same as master, including master itself.

	uint64_t master_hash = get_master_hash(output_peers,group_size);
	int major_cnt =0; // number of majority.
	int ret=0;
	// It provides a best effor decision.
	// If one of hash is 0, just return.
	int zero_count = 0;
	for (i = 0; i < group_size; i++){
		// force hash is different to debug.
		//if (1==i){
		//	output_peers[i].hash=0x55aa;
		//}
		debug_log("[do_decision] leader_id:%u, node_id: %u, hashval: 0x%"PRIx64" hash_index:%ld\n",
			output_peers[i].leader_id,
			output_peers[i].node_id,
			output_peers[i].hash,
			output_peers[i].hash_index);
		fprintf(fp,"[do_decision] leader_id:%u, node_id: %u, hashval: 0x%"PRIx64" hash_index:%ld\n",
			output_peers[i].leader_id,
			output_peers[i].node_id,
			output_peers[i].hash,
			output_peers[i].hash_index);
		fflush(fp);
		if (0L == output_peers[i].hash){
			zero_count++;
		}
	}
	if (zero_count){
		fprintf(fp,"[do_decision] failed to make decision since one of hash is 0\n");
		debug_log("[do_decision] failed to make decision since one of hash is 0\n");
		// 0 means do nothing.
		return 0;
	}
	threshold = group_size/2 +1;
	/*
	Design Discuss:
	1. In this function, I need know who is the leader. Because Decision 3,4 need know whether the hash of leader is same as others.
	2. Right now, there is an assumption that leader_id is 0.
	3. [Solved] by calling get_master_hash()
	*/
	con_num = count_hash(output_peers,group_size,master_hash);
	if (con_num == group_size){ // D.0 all hash are the same.
		fprintf(fp,"[do_decision] D.0 All hash are the same (Nothing to do)\n");
		debug_log("[do_decision] D.0 All hash are the same (Nothing to do)\n");
		ret=0;
		return ret;
	}
	if (con_num >= threshold ){ // // D.1 H(header) == H(major).
		fprintf(fp,"[do_decision] D.1 Minority need redo.\n");
		debug_log("[do_decision] D.1 Minority need redo.\n");
		ret=1;
		do_restore(output_peers,group_size,master_hash);
		return ret;	
	}
	uint64_t major_hash=0;
	major_cnt = major_count_hash(output_peers, group_size, &major_hash); 
	if (major_cnt >= threshold){ // D2. H(header) != H(major).
    	fprintf(fp,"[do_decision] D.2 Master and Minority need redo. and major_hash is 0x%"PRIx64"\n",major_hash);
    	debug_log("[do_decision] D.2 Master and Minority need redo. and major_hash is 0x%"PRIx64"\n",major_hash);
		ret=2;
		do_restore(output_peers,group_size,major_hash);
		return ret;
    }
	// consensus failed
	fprintf(fp,"[hash_consensus] D.3 All nodes need redo.\n");
	debug_log("[hash_consensus] D.3 All nodes need redo.\n");
	ret = 3;
	do_restore(output_peers,group_size,0);
	fflush(fp);
	return ret;
}
