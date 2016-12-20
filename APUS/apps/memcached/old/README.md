Memcached and mcperf requires libevent-dev g++ make wget

1. Install memcached and mcperf in the current directory
  > ./mk

2. Run memcached and test set operation using mcperf
  > ./run
  
The average RTT of memcached is around 87.74us

Output Result:
Total: connections 1 requests 100000 responses 100000 test-duration 8.774 s

Connection rate: 0.1 conn/s (8774.3 ms/conn <= 1 concurrent connections)
Connection time [ms]: avg 8774.3 min 8774.3 max 8774.3 stddev 0.00
Connect time [ms]: avg 0.1 min 0.1 max 0.1 stddev 0.00

Request rate: 11396.9 req/s (0.1 ms/req)
Request size [B]: avg 38.0 min 38.0 max 38.0 stddev 0.00

Response rate: 11396.9 rsp/s (0.1 ms/rsp)
Response size [B]: avg 8.0 min 8.0 max 8.0 stddev 0.00
Response time [ms]: avg 0.1 min 0.0 max 6.5 stddev 0.00
Response time [ms]: p25 1.0 p50 1.0 p75 1.0
Response time [ms]: p95 1.0 p99 1.0 p999 2.0
Response type: stored 100000 not_stored 0 exists 0 not_found 0
Response type: num 0 deleted 0 end 0 value 0
Response type: error 0 client_error 0 server_error 0

Errors: total 0 client-timo 0 socket-timo 0 connrefused 0 connreset 0
Errors: fd-unavail 0 ftab-full 0 addrunavail 0 other 0

CPU time [s]: user 3.26 system 4.94 (user 37.2% system 56.3% total 93.5%)
Net I/O: bytes 4.4 MB rate 512.0 KB/s (4.2*10^6 bps)

