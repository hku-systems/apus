#!/bin/bash

function error_exit() {
  echo "Usage: test.sh -n=<n> -s=<s> -c=<c> <list of username@server addresses>
  n: the number of messages to be sent by each connection.
  s: the size of a piece of message in bytes.
  c: the number of concurrent connections." >&2;
  exit 1
}

if [[ $# -lt 4 ]]; then
  error_exit
fi

while getopts ":n:s:c:" opt; do
  case $opt in
    n) numMessages="$OPTARG"
    ;;
    s) messageSize="$OPTARG"
    ;;
    c) numConnections="$OPTARG"
    ;;
    \?) error_exit
    ;;
  esac
done

if [[ -z "$numMessages" || -z "$numConnections" ]] || [[ -z "$messageSize" ]]; then
  error_exit
fi

numberReplica=`expr $# - 3`
APP_DIR=$RDMA_ROOT/apps/test/bin
REMOTE_PREPARE_COMMAND="sed -i '3c group_size = $numberReplica' $RDMA_ROOT/RDMA/target/nodes.local.cfg; rm -rf DB_node_test*"
REMOTE_RUN_COMMAND="env node_id=$j LD_LIBRARY_PATH=$RDMA_ROOT/RDMA/.local/lib cfg_path=$RDMA_ROOT/RDMA/target/nodes.local.cfg LD_PRELOAD=$RDMA_ROOT/RDMA/target/interpose.so $APP_DIR/server 6379 $numConnections $numMessages $messageSize"
LOCAL_RUN_COMMAND="$APP_DIR/client 6379 $numConnections $numMessages $messageSize"

i=4
j=0
while [ "$i" -le "$#" ]; do
  eval "addr=\${$i}"
  ssh -f $addr $REMOTE_PREPARE_COMMAND
  sleep 2
  ssh -f $addr $REMOTE_RUN_COMMAND
  i=$((i + 1))
  j=$((j + 1))
  sleep 2
done

$LOCAL_RUN_COMMAND
