#!/bin/bash

function error_exit() {
  echo "Usage: test.sh -n=<n> -s=<s> -c=<c> <list of username@server>
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

APP_DIR=$RDMA_ROOT/apps/test/bin
REMOTE_RUN_COMMAND="env node_id=$j LD_LIBRARY_PATH=$RDMA_ROOT/... LD_PRELOAD=$RDMA_ROOT/... $APP_DIR/server 6379 $numConnections $numMessages $messageSize"
LOCAL_RUN_COMMAND="$APP_DIR/client 6379 $numConnections $numMessages $messageSize"

i=4
j=0
while [ "$i" -le "$#" ]; do
  eval "addr=\${$i}"
  ssh -f $addr $REMOTE_RUN_COMMAND
  i=$((i + 1))
  j=$((j + 1))
  sleep 2
done

$LOCAL_RUN_COMMAND
