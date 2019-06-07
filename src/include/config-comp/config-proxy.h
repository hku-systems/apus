#ifndef CONFIG_PROXY_H
#define CONFIG_PROXY_H

struct proxy_node_t;

int proxy_read_config(struct proxy_node_t* cur_node,const char* config_path);

#endif
