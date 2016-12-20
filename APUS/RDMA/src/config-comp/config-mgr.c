#include "../include/util/common-header.h"
#include "../include/ev_mgr/ev_mgr.h"
#include <libconfig.h>

int mgr_read_config(struct event_manager_t* cur_node,const char* config_path){
    config_t config_file;
    config_init(&config_file);

    if(!config_read_file(&config_file,config_path)){
        goto goto_config_error;
    }
    
    uint32_t group_size;
    if(!config_lookup_int(&config_file,"group_size",(int*)&group_size)){
        goto goto_config_error;
    }

    if(group_size<=cur_node->node_id){
        err_log("EVENT MANAGER : Invalid Node Id\n");
        goto goto_config_error;
    }

    config_setting_t *mgr_global_config = NULL;
    mgr_global_config = config_lookup(&config_file,"mgr_global_config");
    
    if(NULL!=mgr_global_config){
        int rsm;
        if(config_setting_lookup_int(mgr_global_config,"rsm",&rsm)){
            cur_node->rsm = rsm;
        }
        int check_output;
        if(config_setting_lookup_int(mgr_global_config,"check_output",&check_output)){
            cur_node->check_output = check_output;
        }
    }

    config_setting_t *mgr_config = NULL;
    mgr_config = config_lookup(&config_file,"mgr_config");

    if(NULL==mgr_config){
        err_log("EVENT MANAGER : Cannot Find Nodes Settings.\n");
        goto goto_config_error;
    }    

    config_setting_t *mgr_ele = config_setting_get_elem(mgr_config,cur_node->node_id);

    if(NULL==mgr_ele){
        err_log("EVENT MANAGER : Cannot Find Current Node's Address Section.\n");
        goto goto_config_error;
    }

// read the option for log, if it has some sections
    
    config_setting_lookup_int(mgr_ele,"time_stamp_log",&cur_node->ts_log);
    config_setting_lookup_int(mgr_ele,"sys_log",&cur_node->sys_log);
    config_setting_lookup_int(mgr_ele,"stat_log",&cur_node->stat_log);
    config_setting_lookup_int(mgr_ele,"req_log",&cur_node->req_log);

    const char* peer_ipaddr=NULL;
    int peer_port=-1;

    if(!config_setting_lookup_string(mgr_ele,"ip_address",&peer_ipaddr)){
        err_log("EVENT MANAGER : Cannot Find Current Node's IP Address.\n")
        goto goto_config_error;
    }

    if(!config_setting_lookup_int(mgr_ele,"port",&peer_port)){
        err_log("EVENT MANAGER : Cannot Find Current Node's Port.\n")
        goto goto_config_error;
    }

    cur_node->sys_addr.s_addr.sin_port = htons(peer_port);
    cur_node->sys_addr.s_addr.sin_family = AF_INET;
    inet_pton(AF_INET,peer_ipaddr,&cur_node->sys_addr.s_addr.sin_addr);
    cur_node->sys_addr.s_sock_len = sizeof(cur_node->sys_addr.s_addr);

    const char* db_name;
    if(!config_setting_lookup_string(mgr_ele,"db_name",&db_name)){
        goto goto_config_error;
    }
    size_t db_name_len = strlen(db_name);
    cur_node->db_name = (char*)malloc(sizeof(char)*(db_name_len+1));
    if(cur_node->db_name==NULL){
        goto goto_config_error;
    }
    if(NULL==strncpy(cur_node->db_name,db_name,db_name_len)){
        free(cur_node->db_name);
        goto goto_config_error;
    }
    cur_node->db_name[db_name_len] = '\0';

    config_destroy(&config_file);
    return 0;

goto_config_error:
    err_log("%s:%d - %s\n", config_error_file(&config_file),
            config_error_line(&config_file), config_error_text(&config_file));
    config_destroy(&config_file);
    return -1;
}