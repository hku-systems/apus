#!/bin/sh 

set -x
#start redis firstly
sh ./stop.sh
sleep 1
ps -elf | grep redis

self_id=`cat node_id`
cfg_path=./nodes.local.cfg
#app_cmd="env LD_LIBRARY_PATH=. LD_PRELOAD= node_id=$self_id cfg_path=$cfg_path ./apps/redis-server ./apps/redis.conf "
#app_cmd="setsid env LD_LIBRARY_PATH=. LD_PRELOAD=./interpose.so node_id=$self_id cfg_path=$cfg_path ./apps/redis-server ./apps/redis.conf "
app_cmd="env LD_LIBRARY_PATH=. LD_PRELOAD=./interpose.so node_id=$self_id cfg_path=$cfg_path ./apps/redis-server ./apps/redis.conf "
#$app_cmd < /dev/null &> start.rdma.log &
$app_cmd &
sleep 3 

aim_name="redis-server"
app_pid=`ps -ef | grep $aim_name | grep -v grep | awk '{print $2}' | head -n 1`
ls -la /proc/$app_pid/fd

lsof -p $app_pid
#python guard.py node_id aim_name rdma.cfg

python guard.py $self_id $aim_name $cfg_path
