#!/bin/sh
# Test.1
udaddy
# Test.2
rdma_server
# Test.3
ib_send_bw -a  -F --report_gbits

# Test.4
rping -s -C 10 -v

# Test.5
ucmatose

# Test.6
ib_send_lat -a  -F --report_gbits
