#!/bin/bash

RDMA_ROOT=`pwd`
RDMA_ROOT=${RDMA_ROOT%%/apps*}

sed -i '$a export RDMA_ROOT='"$RDMA_ROOT"'' ~/.bashrc

source ~/.bashrc

