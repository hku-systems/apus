#!/bin/bash

function error_exit() {
  echo "Usage: test2.sh -p<p> <list of username@server addresses>
  p: the percentage of bandwidth for other traffic." >&2;
  exit 1
}

if [[ $# -lt 3 ]]; then
  error_exit
fi

while getopts ":p:" opt; do
  case $opt in
    p) trafficPer="$OPTARG"
    ;;
    \?) error_exit
    ;;
  esac
done

if [[ -z "$trafficPer" ]]; then
  error_exit
fi

numberReplica=`expr $# - 1`
trafficPara=`echo "$trafficPer * 0.64" | bc`
APP_DIR=$RDMA_ROOT/apps/ssdb/ssdb-master
REMOTE_PREPARE_COMMAND="killall -9 iperf; sed -i '3c group_size = $numberReplica;' $RDMA_ROOT/RDMA/target/nodes.local.cfg; rm -rf DB_node_test*"
POST_COMMAND="killall -9 iperf"
GEN_TRAFFIC_SERVER="iperf -s"
GEN_TRAFFIC_CLIENT="iperf -c 10.22.1.1 -l $trafficPara -t 9999"
LOCAL_RUN_COMMAND="$APP_DIR/tools/sshd-bench 127.0.0.1 8888 10000 50"

i=2
j=0
while [ "$i" -le "$#" ]; do
  eval "addr=\${$i}"
  ssh -f $addr $REMOTE_PREPARE_COMMAND
  sleep 2
  if [ "$i" -eq "2" ]; then
    ssh -f $addr $GEN_TRAFFIC_SERVER
  fi
  if [ "$i" -eq "3" ]; then
    ssh -f $addr $GEN_TRAFFIC_CLIENT
  fi
  REMOTE_RUN_COMMAND="env node_id=$j LD_LIBRARY_PATH=$RDMA_ROOT/RDMA/.local/lib cfg_path=$RDMA_ROOT/RDMA/target/nodes.local.cfg LD_PRELOAD=$RDMA_ROOT/RDMA/target/interpose.so $APP_DIR/ssdb-server $APP_DIR/ssdb.conf"
  ssh -f $addr $REMOTE_RUN_COMMAND
  i=$((i + 1))
  j=$((j + 1))
  sleep 2
done

sleep 5
$LOCAL_RUN_COMMAND 1>$RDMA_ROOT/eval/traffic_test_result.dat 2>&1

i=2
j=0
while [ "$i" -le "$#" ]; do
  eval "addr=\${$i}"
  ssh -f $addr $POST_COMMAND
  i=$((i + 1))
  j=$((j + 1))
  sleep 2
done
