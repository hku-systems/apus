#include "../include/util/common-header.h"
#include "../include/replica-sys/node.h"
#include "../include/config-comp/config-comp.h"

#include "../include/rdma/dare_ibv.h"
#include "../include/rdma/dare_server.h"

#include <sys/stat.h>

#define UNKNOWN_LEADER 9999


int launch_replica_thread(node* my_node, list* excluded_fds, list* excluded_threads)
{
    my_node->cur_view.view_id = 1;
    my_node->cur_view.req_id = 0;
    my_node->cur_view.leader_id = 0;//UNKNOWN_LEADER;

    //zoo_fd = (int*)malloc(sizeof(int));
    //start_zookeeper(&my_node->cur_view, zoo_fd, my_node->zoo_port);
    //listAddNodeTail(excluded_fds, (void*)zoo_fd);
    if (my_node->consensus_comp == NULL)
    {
	fprintf(stderr, "init consensus comp is wrong\n");
	exit(1);
    }
    if (my_node->node_id != 0)
	pthread_create(&my_node->rep_thread,NULL,handle_accept_req,my_node->consensus_comp);
    pthread_t *replica_thread = (pthread_t*)malloc(sizeof(pthread_t));
    *replica_thread = my_node->rep_thread;
    listAddNodeTail(excluded_threads, (void*)replica_thread);
    return 0;
}

int initialize_node(node* my_node, const char* log_path, void (*user_cb)(db_key_type index,void* arg), int (*up_check)(void* arg), int (*up_get)(view_stamp clt_id,void* arg), void* db_ptr, void* arg){

    int flag = 1;

    dare_server_input_t input = {
        .log = stdout,
        .peer_pool = my_node->peer_pool,
        .group_size = my_node->group_size,
        .server_idx = my_node->node_id
    };

    if (0 != dare_server_init(&input)) {
        err_log("CONSENSUS MODULE : Cannot init dare\n");
        goto initialize_node_exit;
    }

    int build_log_ret = 0;
    if(log_path==NULL){
        log_path = ".";
    }else{
        if((build_log_ret=mkdir(log_path,S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH))!=0){
            if(errno!=EEXIST){
                err_log("CONSENSUS MODULE : Log Directory Creation Failed,No Log Will Be Recorded.\n");
            }else{
                build_log_ret = 0;
            }
        }
    }
    if(!build_log_ret){
            char* sys_log_path = (char*)malloc(sizeof(char)*strlen(log_path)+50);
            memset(sys_log_path,0,sizeof(char)*strlen(log_path)+50);
            if(NULL!=sys_log_path){
                sprintf(sys_log_path,"%s/node-%u-consensus-sys.log",log_path,my_node->node_id);
                my_node->sys_log_file = fopen(sys_log_path,"w");
                free(sys_log_path);
            }
            if(NULL==my_node->sys_log_file && (my_node->sys_log || my_node->stat_log)){
                err_log("CONSENSUS MODULE : System Log File Cannot Be Created.\n");
            }
    }

    my_node->consensus_comp = NULL;

    my_node->consensus_comp = init_consensus_comp(my_node,
            my_node->node_id,my_node->sys_log_file,my_node->sys_log,
            my_node->stat_log,my_node->db_name,db_ptr,my_node->group_size,
            &my_node->cur_view,&my_node->highest_to_commit,&my_node->highest_committed,
            &my_node->highest_seen,user_cb,up_check,up_get,arg);
    if(NULL==my_node->consensus_comp){
        goto initialize_node_exit;
    }
    
    flag = 0;
initialize_node_exit:

    return flag;
}

dare_log_entry_t* rsm_op(node* my_node, size_t ret, void *buf, uint8_t type, view_stamp* clt_id)
{
    return leader_handle_submit_req(my_node->consensus_comp,ret,buf,type,clt_id);
}

uint32_t get_leader_id(node* my_node)
{
    return my_node->cur_view.leader_id;
}

uint32_t get_group_size(node* my_node)
{
    return my_node->group_size;
}

node* system_initialize(node_id_t node_id,const char* config_path, const char* log_path, void(*user_cb)(db_key_type index,void* arg), int(*up_check)(void* arg), int(*up_get)(view_stamp clt_id, void*arg), void* db_ptr,void* arg){

    node* my_node = (node*)malloc(sizeof(node));
    memset(my_node,0,sizeof(node));
    if(NULL==my_node){
        goto exit_error;
    }

    my_node->node_id = node_id;
    my_node->db_ptr = db_ptr;

    if(consensus_read_config(my_node,config_path)){
        err_log("CONSENSUS MODULE : Configuration File Reading Failed.\n");
        goto exit_error;
    }


    if(initialize_node(my_node,log_path,user_cb,up_check,up_get,db_ptr,arg)){
        err_log("CONSENSUS MODULE : Network Layer Initialization Failed.\n");
        goto exit_error;
    }

    return my_node;

exit_error:
    if(NULL!=my_node){
    }

    return NULL;
}
