#!/bin/sh

Master_IP=10.22.1.1

udaddy -s $Master_IP

rdma_client  $Master_IP

ib_send_bw -F --report_gbits $Master_IP

rping -c -a $Master_IP -C 10 -v

ucmatose -s $Master_IP

ib_send_lat -a -F --report_gbits $Master_IP

