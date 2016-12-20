#!/usr/bin/env python

import sys

qps_sum=0
qps_cnt=0
for line in sys.stdin:
    if not line:
        break
    if line.startswith("==="):
        #print line
	pass
    if line.startswith("qps"):
        #print line
        parts = line.split(",")
        if len(parts) !=2:
            break
        p = parts[0].split()
        qps = p[1]
        p = parts[1].split()
        t = p[1]
        qps = float(qps)
        t = float(t)
        #ans0 = 1000.0*1000.0/qps
        #ans1 = t*1000.0*1000.0/10000.0 # we sent 10000 requests
        #print "one request time(us): ",ans0," ",ans1
	qps_cnt+=1
	qps_sum+=qps

qps_avg= qps_sum/qps_cnt
print "throughput: ",qps_avg
print "latency: ","Not supported yet"
