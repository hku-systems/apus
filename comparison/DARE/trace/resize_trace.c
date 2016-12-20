/**                                                                                                      
 * DARE (Direct Access REplication)
 *
 * Resize trace generator
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dare.h>
#include <dare_kvs_sm.h>

#define WRITE_COUNT 3
#define LOOP_COUNT 5
//#define MAX_DATA_LEN 1024
#define MAX_DATA_LEN 8

int main(int argc, char* argv[])
{
    if ( argc < 2 )
    {
        printf("Usage: %s <trace_file>\n", argv[0]);
        return 1;
    }
    char *trace_file = argv[1];
    
    FILE *fp;

    fp = fopen(trace_file, "wb");
    if (NULL == fp) {
        printf("Unable to open file!");
        return 1;
    }
    
    uint8_t type;
    uint8_t size;
    
    type = DOWNSIZE;
    fwrite(&type, 1, 1, fp);
    size = 5;
    fwrite(&size, 1, 1, fp);
    
    type = DOWNSIZE;
    fwrite(&type, 1, 1, fp);
    size = 3;
    fwrite(&size, 1, 1, fp);
    
    type = DOWNSIZE;
    fwrite(&type, 1, 1, fp);
    size = 5;
    fwrite(&size, 1, 1, fp);
    
    type = DOWNSIZE;
    fwrite(&type, 1, 1, fp);
    size = 3;
    fwrite(&size, 1, 1, fp);
    
    fclose(fp);
    
    return 0;
}


