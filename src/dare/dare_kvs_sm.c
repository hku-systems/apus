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

#include <stdlib.h>
#include <string.h>

#include "../include/dare/dare_sm.h"
#include "../include/dare/dare_kvs_sm.h"
#include "../include/dare/dare.h"

uint32_t kvs_size; // kvs size in bytes

struct kvs_list_t {
    kvs_entry_t entry;
    struct kvs_list_t *next;
};
typedef struct kvs_list_t kvs_list_t;

struct kvs_table_t {
    uint32_t size;
    kvs_list_t **table;
};
typedef struct kvs_table_t kvs_table_t;

struct dare_kvs_sm_t {
    dare_sm_t   sm;
    kvs_table_t kvs_table;
};
typedef struct dare_kvs_sm_t dare_kvs_sm_t;

struct kvs_snapshot_entry_t {
    uint16_t   len;
    char       key[KEY_SIZE];
    uint8_t    value[0];
};
typedef struct kvs_snapshot_entry_t kvs_snapshot_entry_t;

/* ================================================================== */

static int
create_kvs_table( kvs_table_t* kvs_table );
static void 
destroy_kvs_sm( dare_sm_t* sm );
static int 
apply_kvs_cmd( dare_sm_t *sm, sm_cmd_t *cmd, sm_data_t *data );

static uint32_t 
hash( kvs_table_t *kvs_table, char *key );
static kvs_list_t* 
lookup_key( kvs_table_t *kvs_table, char *key );
static void 
remove_key( kvs_table_t *kvs_table, char *key );
static int 
write_key( kvs_table_t *kvs_table, char *key, kvs_blob_t *blob );

/* ================================================================== */
/* Create KVS */

dare_sm_t* create_kvs_sm( uint32_t size )
{

    int rc;
    dare_kvs_sm_t *kvs_sm;
    
    if (0 == size) {
        size = DEFAULT_KVS_SIZE;
    }
    
    /* Allocate new KVS SM */
    kvs_sm = (dare_kvs_sm_t*)malloc(sizeof(dare_kvs_sm_t));
    if (NULL == kvs_sm) {
        error(log_fp, "Cannot allocate KVS SM\n");
        return NULL;
    }
    
    /* Initiate KVS table */
    kvs_sm->kvs_table.size = size;
    rc = create_kvs_table(&kvs_sm->kvs_table);
    if (0 != rc) {
        free(kvs_sm);
        kvs_sm = NULL;
        error(log_fp, "Cannot allocate KVS SM\n");
        return NULL;
    }

    dare_sm_t sm = {
        .destroy   = destroy_kvs_sm,
        .apply_cmd = apply_kvs_cmd,
    };

    memcpy(&kvs_sm->sm, &sm, sizeof(dare_sm_t));
    
   // kvs_sm->sm.destroy = destroy_kvs_sm;
   // kvs_sm->sm.apply_cmd = apply_kvs_cmd;
    
    return &(kvs_sm->sm);
}

static int
create_kvs_table( kvs_table_t* kvs_table )
{    
    kvs_table->table = (kvs_list_t**)
        malloc(sizeof(kvs_list_t*) * kvs_table->size);
    if (NULL == kvs_table->table) {
        error_return(1, log_fp, "Cannot allocate KVS table\n");
    }
    memset(kvs_table->table, 0, sizeof(kvs_list_t*) * kvs_table->size);
    
    return 0;
}

/* ================================================================== */
/* SM methods */

static void 
destroy_kvs_sm( dare_sm_t* sm )
{
    uint32_t i;
    dare_kvs_sm_t *kvs_sm = (dare_kvs_sm_t*)sm;
    kvs_list_t *list, *tmp;

    if (NULL == kvs_sm) {
        return;
    }
    if (NULL == kvs_sm->kvs_table.table) {
        free(kvs_sm);
        kvs_sm = NULL;
        return;
    }
    for (i = 0; i < kvs_sm->kvs_table.size; i++) {
        list = kvs_sm->kvs_table.table[i];
        while (NULL != list) {
            tmp = list;
            list = list->next;
            if (NULL != tmp->entry.blob.data) {
                free(tmp->entry.blob.data);
                tmp->entry.blob.data = NULL;
            }
            free(tmp);
        }
    }

    free(kvs_sm->kvs_table.table);
    kvs_sm->kvs_table.table = NULL;
    free(kvs_sm);
    kvs_sm = NULL;
}

static int 
apply_kvs_cmd( dare_sm_t* sm, sm_cmd_t *cmd, sm_data_t *data )
{
    int rc;
    kvs_blob_t blob;
    kvs_list_t* list;
    dare_kvs_sm_t *kvs_sm = (dare_kvs_sm_t*)sm;
    if (NULL == kvs_sm) {
        error_return(1, log_fp, "SM is NULL\n");
    }

    kvs_cmd_t *kvs_cmd = (kvs_cmd_t*)cmd->cmd;
    if (NULL == kvs_cmd) {
        error_return(1, log_fp, "Command is NULL\n");
    }
    //debug(log_fp, "KVS type %"PRIu8"\n", kvs_cmd->type);
    switch (kvs_cmd->type) {
        case KVS_PUT:
            //debug(log_fp, "PUT key = %s\n", kvs_cmd->key);
            blob.len = kvs_cmd->len;
            blob.data = kvs_cmd->data;
            rc = write_key(&kvs_sm->kvs_table, kvs_cmd->key, &blob);
            if (0 != rc) {               
                error_return(1, log_fp, "Cannot apply PUT operation\n");
            }
            break;
        case KVS_GET:
            //debug(log_fp, "GET key = %s\n", kvs_cmd->key);
            list = lookup_key(&kvs_sm->kvs_table, kvs_cmd->key);
            if (NULL == list) {
                data->len = 0;
            }
            else {
                data->len = list->entry.blob.len;
                memcpy(data->data, list->entry.blob.data, data->len);
            }
            break;
        case KVS_RM:
            remove_key(&kvs_sm->kvs_table, kvs_cmd->key);
            break;
        default:
            error_return(1, log_fp, "Unknown KVS command\n");
    }
    
    return 0;
}

/* ================================================================== */

/**
 * Simple hash function
 */
static uint32_t 
hash( kvs_table_t *kvs_table, char *key )
{
    uint32_t hashval;
    
    hashval = 0;
    for(; *key != '\0'; key++) {
        hashval = *key + (hashval << 5) - hashval;
    }
    return hashval % kvs_table->size;
}

static kvs_list_t* 
lookup_key( kvs_table_t *kvs_table, char *key )
{
    kvs_list_t *list;
    uint32_t hashval = hash(kvs_table, key);

    for(list = kvs_table->table[hashval]; list != NULL; list = list->next) {
        if (strcmp(key, list->entry.key) == 0) {
            return list;
        }
    }
    return NULL;
}

static void 
remove_key( kvs_table_t *kvs_table, char *key )
{
    kvs_list_t *list, *prev;
    uint32_t hashval = hash(kvs_table, key);
    
    list = kvs_table->table[hashval];
    if (list == NULL) return;
    if (strcmp(key, list->entry.key) == 0) {
        kvs_table->table[hashval] = list->next;
        /* Update KVS size */
        kvs_size = kvs_size - sizeof(kvs_snapshot_entry_t) 
                    - list->entry.blob.len;
        if (NULL != list->entry.blob.data) {
            free(list->entry.blob.data);
            list->entry.blob.data = NULL;
        }
        free(list);
        return;
    }
    prev = list;
    for(list = list->next; list != NULL; list = list->next) {
        if (strcmp(key, list->entry.key) == 0) {
            prev->next = list->next;
            /* Update KVS size */
            kvs_size = kvs_size - sizeof(kvs_snapshot_entry_t) 
                        - list->entry.blob.len;
            if (NULL != list->entry.blob.data) {
                free(list->entry.blob.data);
                list->entry.blob.data = NULL;
            }
            free(list);
            return;
        }
        prev = list;
    }
}

static int 
write_key( kvs_table_t *kvs_table, char *key, kvs_blob_t *blob )
{
    /* Search for list entry with this key */
    kvs_list_t *list = lookup_key(kvs_table, key);
    if (NULL != list) {
        /* Key already exists - overwrite */
        if (list->entry.blob.len != blob->len) {
            /* Update KVS size */
            kvs_size += blob->len - list->entry.blob.len;
            /* Resize blob */
            list->entry.blob.len = blob->len;
            /* Reallocate memory for the value */
            list->entry.blob.data = realloc(list->entry.blob.data, blob->len);
            if (NULL == list->entry.blob.data) {
                error_return(1, log_fp, "Cannot allocate new KVS blob\n");
            }
        }
        memcpy(list->entry.blob.data, blob->data, blob->len);
        return 0;
    }
    
    /* Insert new key */
    unsigned int hashval = hash(kvs_table, key);
    list = (kvs_list_t*)malloc(sizeof(kvs_list_t));
    if (NULL == list) {
        error_return(1, log_fp, "Cannot allocate new KVS list\n");
    }
    memcpy(&list->entry.key, key, KEY_SIZE);
    list->entry.blob.len = blob->len;
    /* Update KVS size */
    kvs_size += sizeof(kvs_snapshot_entry_t) + blob->len;
    /* Allocate memory for the value */
    list->entry.blob.data = malloc(blob->len);
    if (NULL == list->entry.blob.data) {
        error_return(1, log_fp, "Cannot allocate new KVS blob\n");
    }
    memcpy(list->entry.blob.data, blob->data, blob->len);
    list->next = kvs_table->table[hashval];
    kvs_table->table[hashval] = list;

    return 0;
}
