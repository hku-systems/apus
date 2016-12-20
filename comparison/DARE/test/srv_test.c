/**                                                                                                      
 * DARE (Direct Access REplication)
 *
 * Test code for a server
 *
 * Copyright (c) 2016 HLRS, University of Stuttgart. All rights reserved.
 * 
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 *            Nakul Vyas <mailnakul@gmail.com>
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <getopt.h>
#include <dare_server.h>
#include <dare_ibv.h>

FILE *log_fp;
extern char* global_mgid;

void usage( char *prog )
{
    printf("Usage: %s [OPTIONS]\n"
            "OPTIONS:\n"
            "\t--null | --kvs | --fs                    (SM type; default KVS)\n"
            "\t--start | --join | --loggp               (Server type; default start)\n"
            "\t-n <hostname>                            (server's name)\n"
            "\t-s <size>                                (group size; default 3)\n"
            "\t-i <index>                               (server's index)\n"
            "\t-o <output>                              (output file)\n"
            "\t-l <log>                                 (log file)\n"
            "\t-m <mgid>                                (Global Multicast ID)\n"  
            ,prog);
}
int main(int argc, char* argv[])
{
    int rc; 
    char *log_file="";
    dare_server_input_t input = {
        .log = stdout,
        .name = "",
        .output = "dare_servers.out",
        .srv_type = SRV_TYPE_START,
        .sm_type = CLT_KVS,
        .group_size = 3,
        .server_idx = 0xFF
    };
    int c;
    static int srv_type = SRV_TYPE_START;
    static int sm_type = CLT_KVS;
    
    while (1) {
        static struct option long_options[] = {
            /* These options set the type of the SM */
            {"null", no_argument, &sm_type, CLT_NULL},
            {"kvs",  no_argument, &sm_type, CLT_KVS},
            {"fs",   no_argument, &sm_type, CLT_FS},
            /* These options set the type of the server */
            {"start", no_argument, &srv_type, SRV_TYPE_START},
            {"join",  no_argument, &srv_type, SRV_TYPE_JOIN},
            {"loggp",   no_argument, &srv_type, SRV_TYPE_LOGGP},
            /* Other options */
            {"help", no_argument, 0, 'h'},
            {"hostname", required_argument, 0, 'n'},
            {"size", required_argument, 0, 's'},
            {"index", required_argument, 0, 'i'},
            {"output", required_argument, 0, 'o'},
            {"log", required_argument, 0, 'l'},
            {"MGID",required_argument, 0,'m'},
            {0, 0, 0, 0}
        };
        int option_index = 0;
        c = getopt_long (argc, argv, "hnm:s:i:o:l:",
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
                
            case 'n':
                input.name = optarg;
                break;
                
            case 's':
               input.group_size = (uint8_t)atoi(optarg);
               break;
                
            case 'i':
                input.server_idx = (uint8_t)atoi(optarg);
                break;
                
            case 'o':
                input.output = optarg;
                break;
            
            case 'l':
                log_file = optarg;
                break;

            case '?':
                usage(argv[0]);
                break;

            case 'm': 
                global_mgid = optarg;
                break;

            default:
                exit(1);
        }
    }
    input.srv_type = srv_type;
    input.sm_type = sm_type;

    if (strcmp(log_file, "") != 0) {
        input.log = fopen(log_file, "w+");
        if (input.log==NULL) {
            printf("Cannot open log file\n");
            exit(1);
        }
    }
    if (SRV_TYPE_START == input.srv_type) {
        if (0xFF == input.server_idx) {
            printf("A server cannot start without an index\n");
            usage(argv[0]);
            exit(1);
        }
    }
    else if (SRV_TYPE_LOGGP == input.srv_type) {
        if (2 != input.group_size) {
            printf("In loggp mode group_size = 2\n");
            usage(argv[0]);
            exit(1);
        }
        if (0xFF == input.server_idx) {
            printf("A server cannot start without an index\n");
            usage(argv[0]);
            exit(1);
        }
    }
    
    rc = dare_server_init(&input);
    if (0 != rc) {
        fprintf(log_fp, "Cannot init client\n");
        return 1;
    }
    
    fclose(log_fp);
    
    return 0;
}
