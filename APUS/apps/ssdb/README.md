# The Performance Result of SSDB

## Introduction

[SSDB](https://github.com/ideawu/ssdb) is a high performance NoSQL database, which is similar to Redis. 

## Testing Scenario
The testing is running at our server. The client and server are in the same machine. The client will send 10000 requests by 50 threads in each operation, such as set and get, to the server.

```
ssdb-bench 127.0.0.1 8888 10000 50 
```

## Testing Result
Info of client:

```
ssdb-bench - SSDB benchmark tool, 1.9.2
Copyright (c) 2013-2015 ssdb.io

Usage:
    /usr/local/ssdb/ssdb-bench [ip] [port] [requests] [clients]

Options:
    ip          server ip (default 127.0.0.1)
    port        server port (default 8888)
    requests    Total number of requests (default 10000)
    clients     Number of parallel connections (default 50)

```

Result:

```
========== set ==========

qps: 29977, time: 0.334 s

one request time(us):  33.3589084965   33.4
========== get ==========

qps: 65373, time: 0.153 s

one request time(us):  15.2968350848   15.3
========== del ==========

qps: 32698, time: 0.306 s

one request time(us):  30.5829102697   30.6
========== hset ==========

qps: 31667, time: 0.316 s

one request time(us):  31.5786149619   31.6
========== hget ==========

qps: 63088, time: 0.159 s

one request time(us):  15.8508749683   15.9
========== hdel ==========

qps: 32475, time: 0.308 s

one request time(us):  30.7929176289   30.8
========== zset ==========

qps: 31855, time: 0.314 s

one request time(us):  31.3922461152   31.4
========== zget ==========

qps: 62920, time: 0.159 s

one request time(us):  15.8931977114   15.9
========== zdel ==========

qps: 32214, time: 0.310 s

one request time(us):  31.0424039238   31.0
========== qpush ==========

qps: 32056, time: 0.312 s

one request time(us):  31.1954080359   31.2
========== qpop ==========

qps: 32470, time: 0.308 s

one request time(us):  30.7976593779   30.8
```

## Verify
The Testing result at official website is followed. Two results are similar. 

```
#Run on a 2013 MacBook Pro 13 inch with Retina display.

========== set ==========
qps: 44251, time: 0.226 s
========== get ==========
qps: 55541, time: 0.180 s
========== del ==========
qps: 46080, time: 0.217 s
========== hset ==========
qps: 42338, time: 0.236 s
========== hget ==========
qps: 55601, time: 0.180 s
========== hdel ==========
qps: 46529, time: 0.215 s
========== zset ==========
qps: 37381, time: 0.268 s
========== zget ==========
qps: 41455, time: 0.241 s
========== zdel ==========
qps: 38792, time: 0.258 s
```
