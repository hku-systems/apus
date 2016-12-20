#!/bin/sh

make clean;
make;
cat $RDMA_ROOT/apps/env/remote_hosts | while read line
do
    scp interpose.so $LOGNAME@${line}:~/RDMA_Paxos/RDMA/target
done
