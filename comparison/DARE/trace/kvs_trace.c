/**                                                                                                      
 * DARE (Direct Access REplication)
 *
 * KVS trace generator
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

#include <dare.h>
#include <dare_kvs_sm.h>

#define WRITE_COUNT 3
#define LOOP_COUNT 5
//#define MIN_DATA_LEN 8
#define MIN_DATA_LEN 8
#define MAX_DATA_LEN 1024

void usage( char *prog )
{
    printf("Usage: %s [OPTIONS]\n"
            "OPTIONS:\n"
            "\t--loop | --trace  (CLT type; default trace)\n"
            "\t--put | --get     (CMD type for loop)\n"
            "\t-s <size>         (data size in bytes for loop)\n"
            "\t-o <output>       (output file)\n",
            prog);
}

int main(int argc, char* argv[])
{
    int c;
    static int loop = 1;
    static int put = 1;
    uint16_t data_size = 0;
    char *output = "";
    
    while (1) {
        static struct option long_options[] = {
            /* These options set the type of the client */
            {"loop", no_argument, &loop, 1},
            {"trace", no_argument, &loop, 0},
            /* These options set the type of the op */
            {"put", no_argument, &put, 1},
            {"get",  no_argument, &put, 0},
            /* Other options */
            {"help", no_argument, 0, 'h'},
            {"size", required_argument, 0, 's'},
            {"output", required_argument, 0, 'o'},
            {0, 0, 0, 0}
        };
        int option_index = 0;
        c = getopt_long (argc, argv, "hs:o:",
                       long_options, &option_index);
        /* Detect the end of the options. */
        if (c == -1) break;
        switch (c) {
            case 0:
                /* If this option set a flag, do nothing else now. */
                if (long_options[option_index].flag != 0)
                    break;
                printf("option %s", long_options[option_index].name);
                if (optarg)
                    printf (" with arg %s", optarg);
                printf("\n");
                break;

            case 'h':
                usage(argv[0]);
                exit(1);
                
            case 's':
                data_size = (uint16_t)atoi(optarg);
                break;
            
            case 'o':
                output = optarg;
                break;

            case '?':
                break;

            default:
                exit(1);
        }
    }
    
    if (loop) {
        if (data_size > MAX_DATA_LEN) {
            printf("Data size too big (max is %d)\n", MAX_DATA_LEN);
            exit(1);
        }
        if (data_size == 0) {
            printf("Data size required\n");
            usage(argv[0]);
            exit(1);
        }
    }
    
    if (strcmp(output, "") == 0) {
        printf("No output file\n");
        exit(1);
    }
    FILE *fp;
    fp = fopen(output, "wb");
    if (NULL == fp) {
        printf("Unable to open file!");
        return 1;
    }
    
    /**
     * Trace file format (KVS): 
     *      READ/WRITE | PUT/GET/DELETE | KEY | LEN | DATA 
     */
    uint8_t type;
    kvs_cmd_t kvs_cmd;
    char data[MAX_DATA_LEN];
    memset(data, 'x', MAX_DATA_LEN);
    if (loop) {
        /* Add a write so that the read can work */
        //type = CSM_WRITE;
        //kvs_cmd.type = KVS_PUT;
        //memset(kvs_cmd.key, 0, KEY_SIZE);
        //sprintf(kvs_cmd.key, "key%"PRIu16, data_size);
        //kvs_cmd.len = data_size;
        //fwrite(&type, 1, 1, fp);
        //fwrite(&kvs_cmd, sizeof(kvs_cmd_t), 1, fp);
        //fwrite(data, kvs_cmd.len, 1, fp);
        /* Add just one command */
        if (put) {
            type = CSM_WRITE;
            kvs_cmd.type = KVS_PUT;
            memset(kvs_cmd.key, 0, KEY_SIZE);
            sprintf(kvs_cmd.key, "key%"PRIu16, data_size);
            kvs_cmd.len = data_size;
            fwrite(&type, 1, 1, fp);
            fwrite(&kvs_cmd, sizeof(kvs_cmd_t), 1, fp);
            fwrite(data, kvs_cmd.len, 1, fp);
            printf("CMD: [%s] %s key=%s; data len=%"PRIu16"\n", 
              (type == CSM_READ) ? "READ" : "WRITE", 
              (kvs_cmd.type == KVS_PUT) ? "PUT" : 
              (kvs_cmd.type == KVS_GET) ? "GET" : "RM", 
                kvs_cmd.key, kvs_cmd.len); 
            type = CSM_READ;
            kvs_cmd.type = KVS_GET;
            memset(kvs_cmd.key, 0, KEY_SIZE);
            sprintf(kvs_cmd.key, "key%"PRIu16, data_size);
            kvs_cmd.len = 0;
            fwrite(&type, 1, 1, fp);
            fwrite(&kvs_cmd, sizeof(kvs_cmd_t), 1, fp);
        }
        else {
            type = CSM_READ;
            kvs_cmd.type = KVS_GET;
            memset(kvs_cmd.key, 0, KEY_SIZE);
            sprintf(kvs_cmd.key, "key%"PRIu16, data_size);
            kvs_cmd.len = 0;
            fwrite(&type, 1, 1, fp);
            fwrite(&kvs_cmd, sizeof(kvs_cmd_t), 1, fp);
            printf("CMD: [%s] %s key=%s; data len=%"PRIu16"\n", 
              (type == CSM_READ) ? "READ" : "WRITE", 
              (kvs_cmd.type == KVS_PUT) ? "PUT" : 
              (kvs_cmd.type == KVS_GET) ? "GET" : "RM", 
                kvs_cmd.key, kvs_cmd.len);
            type = CSM_WRITE;
            kvs_cmd.type = KVS_PUT;
            memset(kvs_cmd.key, 0, KEY_SIZE);
            sprintf(kvs_cmd.key, "key%"PRIu16, data_size);
            kvs_cmd.len = data_size;
            fwrite(&type, 1, 1, fp);
            fwrite(&kvs_cmd, sizeof(kvs_cmd_t), 1, fp);
            fwrite(data, kvs_cmd.len, 1, fp);
        }

        goto end;
    }
    
    uint16_t size = MIN_DATA_LEN;
    for (;size <= MAX_DATA_LEN; size <<= 1) {
        /* Add write */
        type = CSM_WRITE;
        kvs_cmd.type = KVS_PUT;
        memset(kvs_cmd.key, 0, KEY_SIZE);
        sprintf(kvs_cmd.key, "key%"PRIu16, size);
        kvs_cmd.len = size;
printf("CMD: [%s] %s key=%s; data len=%"PRIu16"\n", 
              (type == CSM_READ) ? "READ" : "WRITE", 
              (kvs_cmd.type == KVS_PUT) ? "PUT" : 
              (kvs_cmd.type == KVS_GET) ? "GET" : "RM", 
                kvs_cmd.key, kvs_cmd.len);
        fwrite(&type, 1, 1, fp);
        fwrite(&kvs_cmd, sizeof(kvs_cmd_t), 1, fp);
        fwrite(data, kvs_cmd.len, 1, fp);
        
        /* Add read */
        type = CSM_READ;
        kvs_cmd.type = KVS_GET;
        memset(kvs_cmd.key, 0, KEY_SIZE);
        sprintf(kvs_cmd.key, "key%"PRIu16, size);
        kvs_cmd.len = 0;
printf("CMD: [%s] %s key=%s; data len=%"PRIu16"\n", 
              (type == CSM_READ) ? "READ" : "WRITE", 
              (kvs_cmd.type == KVS_PUT) ? "PUT" : 
              (kvs_cmd.type == KVS_GET) ? "GET" : "RM", 
                kvs_cmd.key, kvs_cmd.len);
        fwrite(&type, 1, 1, fp);
        fwrite(&kvs_cmd, sizeof(kvs_cmd_t), 1, fp);
#if 0        
        /* Add remove */
        type = CSM_WRITE;
        kvs_cmd.type = KVS_RM;
        memset(kvs_cmd.key, 0, KEY_SIZE);
        sprintf(kvs_cmd.key, "key%"PRIu16, size);
        kvs_cmd.len = 0;
printf("CMD: [%s] %s key=%s; data len=%"PRIu16"\n", 
              (type == CSM_READ) ? "READ" : "WRITE", 
              (kvs_cmd.type == KVS_PUT) ? "PUT" : 
              (kvs_cmd.type == KVS_GET) ? "GET" : "RM", 
                kvs_cmd.key, kvs_cmd.len);
        fwrite(&type, 1, 1, fp);
        fwrite(&kvs_cmd, sizeof(kvs_cmd_t), 1, fp);
#endif 
    }

end:    
    fclose(fp);
    
    return 0;
}


