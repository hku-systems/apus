#!/bin/sh
set -x

rm -rf ./*.so
rm -rf ./*.cfg
#SRC_PATH=/home/hkucs/RDMA_Paxos/RDMA/target
SRC_PATH=/home/jingyu/code/rdma_master/RDMA_Paxos/RDMA/target

cp $SRC_PATH/interpose.so  .
cp $SRC_PATH/nodes.local.cfg .

file $SRC_PATH/interpose.so
file ./interpose.so
