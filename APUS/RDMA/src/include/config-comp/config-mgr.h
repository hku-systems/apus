#ifndef CONFIG_MGR_H
#define CONFIG_MGR_H

struct event_manager_t;

int mgr_read_config(struct event_manager_t* cur_node,const char* config_path);

#endif