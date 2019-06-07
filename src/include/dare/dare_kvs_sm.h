/**                                                                                                      
 * DARE (Direct Access REplication)
 * 
 * State machine implementation (KVS)
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */
 
#ifndef DARE_KVS_SM_H
#define DARE_KVS_SM_H

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "./dare_sm.h"

#define DEFAULT_KVS_SIZE 1024
#define KEY_SIZE 64

/* KVS commands */
#define KVS_PUT 1
#define KVS_GET 2
#define KVS_RM  3

/* KVS command */
struct kvs_cmd_t {
    uint8_t     type;   // read, write, delete
    char        key[KEY_SIZE];
    uint16_t    len;
    uint8_t     data[0];
};
typedef struct kvs_cmd_t kvs_cmd_t;

struct kvs_blob_t {
    uint16_t len;
    void *data;
};
typedef struct kvs_blob_t kvs_blob_t;

struct kvs_entry_t {
    char       key[KEY_SIZE];
    kvs_blob_t blob;
};
typedef struct kvs_entry_t kvs_entry_t;

#endif /* DARE_KVS_SM_H */
