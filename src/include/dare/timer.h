/**
 * DARE (Direct Access REplication)
 * 
 * Timer implementation
 *
 * Copyright (c) 2014-2015 ETH-Zurich. All rights reserved.
 * 
 * Copyright (c) 2009 The Trustees of Indiana University and Indiana
 *                    University Research and Technology
 *                    Corporation.  All rights reserved.
 *
 * Author(s): Torsten Hoefler <htor@cs.indiana.edu>
 */

#ifndef TIMER_H_
#define TIMER_H_

#include "./debug.h"

#define UINT32_T uint32_t
#define UINT64_T uint64_t

#define HRT_CALIBRATE(freq) do {  \
  static volatile HRT_TIMESTAMP_T t1, t2; \
  static volatile UINT64_T elapsed_ticks, min = (UINT64_T)(~0x1); \
  int notsmaller=0; \
  while(notsmaller<3) { \
    HRT_GET_TIMESTAMP(t1); \
     sleep(1);  \
    /* nanosleep((struct timespec[]){{0, 10000000}}, NULL); */ \
    HRT_GET_TIMESTAMP(t2); \
    HRT_GET_ELAPSED_TICKS(t1, t2, &elapsed_ticks); \
    notsmaller++; \
    if(elapsed_ticks<min) { \
      min = elapsed_ticks; \
      notsmaller = 0; \
    } \
  } \
  freq = min; \
} while(0);

#define HRT_INIT(freq) HRT_CALIBRATE(freq)

#define HRT_TIMESTAMP_T x86_64_timeval_t

#define HRT_GET_TIMESTAMP(t1)  __asm__ __volatile__ ("rdtsc" : "=a" (t1.l), "=d" (t1.h));

#define HRT_GET_ELAPSED_TICKS(t1, t2, numptr)   *numptr = (((( UINT64_T ) t2.h) << 32) | t2.l) - \
                                                          (((( UINT64_T ) t1.h) << 32) | t1.l);

#define HRT_GET_TIME(t1, time) time = (((( UINT64_T ) t1.h) << 32) | t1.l)

typedef struct {
    UINT32_T l;
    UINT32_T h;
} x86_64_timeval_t;

/* global timer frequency in Hz */
extern unsigned long long g_timerfreq;

#define HRT_GET_USEC(ticks) 1e6/*1e4*/*(double)ticks/(double)g_timerfreq

#define usecs_wait(d) do {   \
  HRT_TIMESTAMP_T ts;   \
  unsigned long long targettime, time;  \
  HRT_GET_TIMESTAMP(ts);    \
  HRT_GET_TIME(ts,targettime);  \
  targettime += g_timerfreq/1e6*(d);    \
  do {  \
    HRT_GET_TIMESTAMP(ts);  \
    HRT_GET_TIME(ts,time);  \
  } while (time < targettime);  \
} while(0);

#ifdef DEBUG
#define TIMER_INIT HRT_TIMESTAMP_T t1, t2;  \
    uint64_t ticks; \
    double usecs; 
#define TIMER_START(stream, fmt, ...) info_wtime(stream, fmt, ##__VA_ARGS__); \
    HRT_GET_TIMESTAMP(t1);
#define TIMER_STOP(stream) HRT_GET_TIMESTAMP(t2);   \
    HRT_GET_ELAPSED_TICKS(t1, t2, &ticks);  \
    usecs = HRT_GET_USEC(ticks);    \
    info(stream, "done (%lf usecs)\n", usecs);
#define TIMER_INFO(stream, fmt, ...) info(stream, fmt, ##__VA_ARGS__);
#else
#define TIMER_INIT
#define TIMER_START(stream, fmt, ...)
#define TIMER_STOP(stream)
#define TIMER_INFO(stream, fmt, ...)
#endif


#endif /* TIMER_H_ */

