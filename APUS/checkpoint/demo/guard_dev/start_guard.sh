#!/bin/sh

set -x
SCRIPT_DIR=$(dirname $0)
cd $SCRIPT_DIR
aim_name="redis-server"

if [ ! -z "$1" ]; then
	aim_name=$1
fi

ID_IP=`ifconfig eth4 | grep "inet addr" | awk  'BEGIN {FS = ":"; };{print $2}' | awk '{print $1}' | awk 'BEGIN {FS="."; }; {print $4}'`
self_id=`expr $ID_IP - 1 `

if [ ! -z "$2" ]; then
	self_id=$2
fi

cfg_path=../../../RDMA/target/nodes.local.cfg

# remove socket file
SOCK_FILE=/tmp/guard.sock
sudo rm -rf $SOCK_FILE

python ./guard.py $self_id $aim_name $cfg_path >  guard.log 2>&1
