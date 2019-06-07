/**                                                                                                      
 * DARE (Direct Access REplication)
 * 
 * Group configuration
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#ifndef DARE_CONFIG_H
#define DARE_CONFIG_H 

#include "./dare.h"

/* Stable configuration: only one size specified */
#define CID_STABLE  0
/* Transitional configuration: both old and new size are specified; 
 * !!! both majority needed */
#define CID_TRANSIT  1
/* Extended configuration: both old and new size are specified; 
 * !!! only old majority needed */
#define CID_EXTENDED  2

#define CID_IS_SERVER_ON(cid, idx) ((cid).bitmask & (1 << (idx)))
#define CID_SERVER_ADD(cid, idx) (cid).bitmask |= 1 << (idx)
#define CID_SERVER_RM(cid, idx) (cid).bitmask &= ~(1 << (idx))

/** 
 * Configuration ID: A configuration is given by a 
 * [N, N', STATE, BITMASK] tuple, where:
 * N - is the current group size
 * N' - is the new size in a transitional configuration
 * STATE - is the configuration state: stable, transitional, extended
 * BITMASK - is a bitmask with a bit set for every on servers
 */
struct dare_cid_t {
    uint64_t epoch;
    uint8_t size[2];
    uint8_t state;
    uint8_t pad[1];
    uint32_t bitmask;
};
typedef struct dare_cid_t dare_cid_t;

static int
equal_cid( dare_cid_t left_cid, dare_cid_t right_cid )
{
    if (left_cid.epoch != right_cid.epoch) return 0;
    if (left_cid.state != right_cid.state) return 0;
    if (left_cid.size[0] != right_cid.size[0]) return 0;
    if (left_cid.size[1] != right_cid.size[1]) return 0;
    if (left_cid.bitmask != right_cid.bitmask) return 0;
    return 1;
} 

typedef struct server_t server_t;
struct server_config_t {
    dare_cid_t cid;         /* configuration identifier */ 
    uint64_t cid_offset;    /* the offset of the next entry from where 
                            to start looking for CONFIG entries; 
                            note that it cannot be larger than WRITE */
    uint64_t cid_idx;       /* the index of the last CONFIG entry before 
                            joining the cluster; a server considers only
                            CONFIG entries with a larger index */
    uint64_t req_id;        /* Request ID of the endpoint that owns 
                            this configuration change */
    server_t *servers;      /* array with info for each server */
    uint16_t clt_id;        /* LID of the endpoint that owns 
                            this configuration change */
    uint8_t idx;            /* own index in configuration */
    uint8_t len;            /* fixed length of configuration array */
};
typedef struct server_config_t server_config_t;

/* Get the maximum size including the extra added servers */
static uint8_t
get_extended_group_size( server_config_t config )
{
    if (CID_STABLE == config.cid.state)
        return config.cid.size[0];
    if (config.cid.size[0] < config.cid.size[1])
        return config.cid.size[1];
    return config.cid.size[0];
}

/* Get the maximum size ignoring the extra added servers */
static uint8_t
get_group_size( server_config_t config )
{
    if (CID_TRANSIT != config.cid.state)
        return config.cid.size[0];
    if (config.cid.size[0] < config.cid.size[1])
        return config.cid.size[1];
    return config.cid.size[0];
}

#define PRINT_CID(cid) text(log_fp,     \
    " [E%"PRIu64":%02"PRIu8"|%02"PRIu8"|%d|%03"PRIu32"] ", \
    (cid).epoch, (cid).size[0], (cid).size[1], (cid).state, (cid).bitmask)
#define PRINT_CID_(cid) PRINT_CID(cid); text(log_fp, "\n");

#define PRINT_CONF_TRANSIT(old_cid, new_cid) \
    info_wtime(log_fp, "(%s:%d) Configuration transition: " \
        "[E%"PRIu64":%02"PRIu8"|%02"PRIu8"|%d|%03"PRIu32"] -> " \
        "[E%"PRIu64":%02"PRIu8"|%02"PRIu8"|%d|%03"PRIu32"]\n", \
        __func__, __LINE__, \
        (old_cid).epoch, (old_cid).size[0], (old_cid).size[1], \
        (old_cid).state, (old_cid).bitmask, \
        (new_cid).epoch, (new_cid).size[0], (new_cid).size[1], \
        (new_cid).state, (new_cid).bitmask)

#endif /* DARE_CONFIG_H */
