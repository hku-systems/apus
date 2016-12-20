#include "../include/config-comp/config-comp.h"

int consensus_read_config(node* cur_node,const char* config_path){
	config_t config_file;
	config_init(&config_file);

	if(!config_read_file(&config_file,config_path)){
		goto goto_config_error;
	}

	uint32_t group_size;
	if(!config_lookup_int(&config_file,"group_size",(int*)&group_size)){
		goto goto_config_error;
	}

	cur_node->group_size = group_size;
	cur_node->peer_pool = (peer*)malloc(group_size*sizeof(peer));
	if(NULL==cur_node->peer_pool){
		goto goto_config_error;
	}

	if(group_size<=cur_node->node_id){
		err_log("CONSENSUS : Configuration Reading Error : Invalid Node Id.\n");
		goto goto_config_error;
	}

	config_setting_t *nodes_config;
	nodes_config = config_lookup(&config_file,"consensus_config");

	if(NULL==nodes_config){
		err_log("CONSENSUS : Cannot Find Nodes Settings.\n");
		goto goto_config_error;
	}
	if(NULL==nodes_config){
		err_log("CONSENSUS : Cannot Find Net Address Section.\n");
		goto goto_config_error;
	}
	peer* peer_pool = cur_node->peer_pool;
	for(uint32_t i=0;i<group_size;i++){
		config_setting_t *node_config = config_setting_get_elem(nodes_config,i);
		if(NULL==node_config){
			err_log("CONSENSUS : Cannot Find Node%u's Address.\n",i);
			goto goto_config_error;
		}
		const char* peer_ipaddr;
		int peer_port;
		if(!config_setting_lookup_string(node_config,"ip_address",&peer_ipaddr)){
			goto goto_config_error;
		}
		if(!config_setting_lookup_int(node_config,"port",&peer_port)){
			goto goto_config_error;
		}

		peer_pool[i].peer_address = (struct sockaddr_in*)malloc(sizeof(struct
			sockaddr_in));
		peer_pool[i].peer_address->sin_family =AF_INET;
		inet_pton(AF_INET,peer_ipaddr,&peer_pool[i].peer_address->sin_addr);
		peer_pool[i].peer_address->sin_port = htons(peer_port);
		if(i==cur_node->node_id){
			config_setting_lookup_int(node_config,"sys_log",&cur_node->sys_log);
			config_setting_lookup_int(node_config,"stat_log",&cur_node->stat_log);
			const char* db_name;
			if(!config_setting_lookup_string(node_config,"db_name",&db_name)){
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
		}
	}

	config_setting_t *zoo_config = NULL;

	zoo_config = config_lookup(&config_file,"zookeeper_config");
	if(NULL==zoo_config){
		err_log("CONSENSUS : Cannot Find Nodes Settings.\n");
		goto goto_config_error;
	}

	config_setting_t *zoo_ele = config_setting_get_elem(zoo_config,cur_node->node_id);
	if(NULL==zoo_ele){
		err_log("CONSENSUS : Cannot Find Current Node's Address Section.\n");
		goto goto_config_error;
	}

	int peer_port=-1;
	if(!config_setting_lookup_int(zoo_ele,"port",&peer_port)){
		err_log("CONSENSUS : Cannot Find Current Node's Port.\n")
		goto goto_config_error;
	}

	cur_node->zoo_port = peer_port;

	config_destroy(&config_file);

	return 0;

goto_config_error:
	err_log("CONSENSUS : %s:%d - %s\n", config_error_file(&config_file),
		config_error_line(&config_file), config_error_text(&config_file));
	config_destroy(&config_file);
	return -1;
};