#!/bin/bash

function error_exit() {
  echo "Usage: test.sh -s<s> <list of username@server addresses>
  s: the size of a piece of message in bytes." >&2;
  exit 1
}

if [[ $# -lt 2 ]]; then
  error_exit
fi

while getopts ":s:" opt; do
  case $opt in
    s) messageSize="$OPTARG"
    ;;
    \?) error_exit
    ;;
  esac
done

if [[ -z "$messageSize" ]]; then
  error_exit
fi

numberReplica=`expr $# - 1`
APP_DIR=$RDMA_ROOT/apps/test/bin
REMOTE_PREPARE_COMMAND="killall -9 server 1>/dev/null 2>&1; sed -i '3c group_size = $numberReplica;' $RDMA_ROOT/RDMA/target/nodes.local.cfg 1>/dev/null 2>&1; rm -rf DB_node_test* 1>/dev/null 2>&1"
LOCAL_RUN_COMMAND="$APP_DIR/client 6379 1 50 $messageSize"

i=2
j=0
while [ "$i" -le "$#" ]; do
  eval "addr=\${$i}"
  ssh -f $addr $REMOTE_PREPARE_COMMAND
  sleep 2
  REMOTE_RUN_COMMAND="env node_id=$j LD_LIBRARY_PATH=$RDMA_ROOT/RDMA/.local/lib cfg_path=$RDMA_ROOT/RDMA/target/nodes.local.cfg LD_PRELOAD=$RDMA_ROOT/RDMA/target/interpose.so $APP_DIR/server 6379 1 50 $messageSize 1>/dev/null 2>&1"
  ssh -f $addr $REMOTE_RUN_COMMAND
  i=$((i + 1))
  j=$((j + 1))
  sleep 2
done

sleep 5
$LOCAL_RUN_COMMAND 1>$RDMA_ROOT/eval/test_result.dat 2>&1
