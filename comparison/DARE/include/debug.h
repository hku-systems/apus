/**          
 * DARE (Direct Access REplication)
 *                                                                                             
 * Debugging and logging utilities
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Author(s): Marius Poke <marius.poke@inf.ethz.ch>
 * 
 */

#ifndef DEBUG_H_
#define DEBUG_H_

#include <stdio.h>
#include <errno.h>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>
#include <sys/time.h>

//extern struct timeval prev_tv;
//extern uint64_t jump_cnt;

#define info(stream, fmt, ...) do {\
    fprintf(stream, fmt, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
#define info_wtime(stream, fmt, ...) do {\
    struct timeval _debug_tv;\
    gettimeofday(&_debug_tv,NULL);\
/*    if (prev_tv.tv_sec != 0) { \
        double __tmp = (_debug_tv.tv_sec - prev_tv.tv_sec) * 1000 + (_debug_tv.tv_usec -  prev_tv.tv_usec)/1000;\
        if (__tmp > 15) {\
            jump_cnt++;\
            fprintf(stream, "Time jump (%lf) ms %"PRIu64"\n", __tmp, jump_cnt);\
        }\
    }*/\
    fprintf(stream, "[%lu:%06lu] " fmt, _debug_tv.tv_sec, _debug_tv.tv_usec, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)

#ifdef DEBUG
#define debug(stream, fmt, ...) do {\
    struct timeval _debug_tv;\
    gettimeofday(&_debug_tv,NULL);\
    fprintf(stream, "[DEBUG %lu:%lu] %s/%d/%s() " fmt, _debug_tv.tv_sec, _debug_tv.tv_usec, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
#define text(stream, fmt, ...) do {\
    fprintf(stream, fmt, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
#define text_wtime(stream, fmt, ...) do {\
    struct timeval _debug_tv;\
    gettimeofday(&_debug_tv,NULL);\
    fprintf(stream, "[%lu:%lu] " fmt, _debug_tv.tv_sec, _debug_tv.tv_usec, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
#else
#define debug(stream, fmt, ...)
#define text(stream, fmt, ...)
#define text_wtime(stream, fmt, ...)
#endif

//#ifdef DEBUG
#define error(stream, fmt, ...) do { \
    fprintf(stream, "[ERROR] %s/%d/%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)
//#else
//#define error(stream, fmt, ...)
//#endif

//#ifdef DEBUG
#define error_return(rc, stream, fmt, ...) do { \
    fprintf(stream, "[ERROR] %s/%d/%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
    return (rc);  \
} while(0)
//#else
//#define error_return(rc, stream, fmt, ...) return (rc)
//#endif

//#ifdef DEBUG
#define error_exit(rc, stream, fmt, ...) do { \
    fprintf(stream, "[ERROR] %s/%d/%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
    exit(rc); \
} while(0)
//#else
//#define error_exit(rc, stream, fmt, ...) exit(rc)
//#endif

#ifndef DEBUG
#define dump_bytes(stream, addr, len, header) do { \
    uint32_t _i; \
    uint8_t *bytes = (uint8_t*)addr; \
    info(stream, "### %s: [" , header); \
    for (_i = 0; _i < (uint32_t)(len); _i++) { \
        info(stream, "%"PRIu8", ", bytes[_i]); \
    }   \
    info(stream, "]\n"); \
} while(0)
#else
#define dump_bytes(stream, addr, len, header)
#endif

extern FILE *log_fp;

#endif /* DEBUG_H_ */

