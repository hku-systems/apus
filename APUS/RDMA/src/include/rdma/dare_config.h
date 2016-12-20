#ifndef DARE_CONFIG_H
#define DARE_CONFIG_H 

#include "dare.h"
#include <inttypes.h>

struct dare_cid_t {
    uint8_t size;
};
typedef struct dare_cid_t dare_cid_t;

typedef struct server_t server_t;
struct server_config_t {
    dare_cid_t cid;
    server_t *servers;      /* array with info for each server */
    uint32_t idx;            /* own index in configuration */
    uint32_t len;            /* fixed length of configuration array */
};
typedef struct server_config_t server_config_t;

#endif /* DARE_CONFIG_H */
