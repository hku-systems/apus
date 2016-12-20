/**                                                                                                      
 * DARE (Direct Access REplication)
 *
 * Test code for a client
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

#include <dare_client.h>
#include <dare_ibv.h>
#include <dare_kvs_sm.h>

extern char* global_mgid;

void usage( char *prog )
{
    printf("Usage: %s [OPTIONS]\n"
            "OPTIONS:\n"
            "\t--null | --kvs | --fs                    (SM type; default KVS)\n"
            "\t--reconf | --loop | --trace | --rtrace   (CLT type; default trace)\n"
            "\t-p <percentage>                          (percentage of the 1st op; default 100)\n"
            "\t-s <size>                                (new size)\n"
            "\t-t <trace>                               (trace file)\n"
            "\t-o <output>                              (output file)\n"
            "\t-l <log>                                 (log file)\n",
            "\t-m <mgid>                                (Global Multicast ID)\n.",
            prog);
}

int main(int argc, char* argv[])
{
    int rc; 
    char *log_file="";
    dare_client_input_t input = {
        .log = stdout,
        .trace = "",
        .output = "",
        .clt_type = CLT_TYPE_TRACE,
        .sm_type = CLT_KVS,
        .first_op_perc = 100,
        .group_size = 0
    };
    int c;
    static int clt_type = CLT_TYPE_TRACE;
    static int sm_type = CLT_KVS;

    while (1) {
        static struct option long_options[] = {
            /* These options set the type of the SM */
            {"null", no_argument, &sm_type, CLT_NULL},
            {"kvs",  no_argument, &sm_type, CLT_KVS},
            {"fs",   no_argument, &sm_type, CLT_FS},
            /* These options set the type of the Client */
            {"reconf", no_argument, &clt_type, CLT_TYPE_RECONF},
            {"loop",  no_argument, &clt_type, CLT_TYPE_LOOP},
            {"trace",   no_argument, &clt_type, CLT_TYPE_TRACE},
            {"rtrace",   no_argument, &clt_type, CLT_TYPE_RTRACE},
            /* Other options */
            {"help", no_argument, 0, 'h'},
            {"perc", required_argument, 0, 'p'},
            {"size", required_argument, 0, 's'},
            {"trace", required_argument, 0, 't'},
            {"output", required_argument, 0, 'o'},
            {"log", required_argument, 0, 'l'},
            {"MGID",required_argument, 0,'m'},
            {0, 0, 0, 0}
        };
        int option_index = 0;
        c = getopt_long (argc, argv, "hs:m:t:o:l:p:",
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
                input.group_size = (uint8_t)atoi(optarg);
                break;
                
            case 'p':
                input.first_op_perc = (uint8_t)atoi(optarg);
                break;
            
            case 't':
                input.trace = optarg;
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
                usage(argv[0]);
                exit(1);
        }
    }
    input.clt_type = clt_type;
    input.sm_type = sm_type;
    if (input.first_op_perc > 100) {
        printf("The percentage of the 1st op cannot be > 100\n");
        exit(1);
    }
    if (strcmp(log_file, "") != 0) {
        input.log = fopen(log_file, "w+");
        if (NULL == input.log) {
            printf("Cannot open log file\n");
            exit(1);
        }
    }
    if (CLT_TYPE_RECONF == input.clt_type) {
        if (0 == input.group_size) {
            printf("A reconf client requires the new size\n");
            usage(argv[0]);
            exit(1);
        }
    }
    else {
        if ( (strcmp(input.trace, "") == 0) || 
            (strcmp(input.output, "") == 0) ) {
            printf("A non-reconf client requires both trace and output files\n");
            usage(argv[0]);
            exit(1);
        }
    }
    
    rc = dare_client_init(&input);
    if (0 != rc) {
        fprintf(input.log, "Cannot init client\n");
        return 1;
    }
    
    dare_client_shutdown();
    
    return 0;
}
