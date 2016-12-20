#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>

#define DEBUG_LOG

#ifdef DEBUG_LOG
#define debug_log(args...) do { \
    struct timeval tv; \
    gettimeofday(&tv,0); \
    fprintf(stderr,"%lu.%06lu:",tv.tv_sec,tv.tv_usec); \
    fprintf(stderr,args); \
}while(0);
#else
#define debug_log(args...)
#endif

#define err_log(args...) do { \
    struct timeval tv; \
    gettimeofday(&tv,0); \
    fprintf(stderr,"%lu.%06lu:",tv.tv_sec,tv.tv_usec); \
    fprintf(stderr,args); \
}while(0);

#define rec_log(out,args...) do { \
    fprintf((out),args); \
    fflush(out); \
}while(0);

#define safe_rec_log(x,args...) {if(NULL!=(x)){rec_log((x),args);}}

#define SYS_LOG(x,args...) {if((x)->sys_log){safe_rec_log(((x)->sys_log_file),args)}}

#define STAT_LOG(x,args...) {if((x)->stat_log){safe_rec_log(((x)->sys_log_file),args)}}

#define REQ_LOG(x,args...) {if((x)->req_log){safe_rec_log(((x)->sys_log_file),args)}}

#define info(stream, fmt, ...) do {\
    fprintf(stream, fmt, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)

#define rdma_error(stream, fmt, ...) do { \
    fprintf(stream, "[ERROR] %s/%d/%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
} while(0)

#define error_return(rc, stream, fmt, ...) do { \
    fprintf(stream, "[ERROR] %s/%d/%s() " fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
    fflush(stream); \
    return (rc);  \
} while(0)

extern FILE *log_fp;

#endif
