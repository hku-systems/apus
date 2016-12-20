#ifndef CONFIG_COMP_H
#define CONFIG_COMP_H
#include "../util/common-header.h"
#include "../replica-sys/node.h"
#include <libconfig.h>

int consensus_read_config(node* cur_node,const char* config_file);

#endif