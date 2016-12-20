#!/bin/sh 

set -x
#start redis firstly
sh ./stop.sh
sleep 1
ps -elf | grep redis

self_id=`cat node_id`
cfg_path=./nodes.local.cfg
#app_cmd="env LD_LIBRARY_PATH=. LD_PRELOAD= node_id=$self_id cfg_path=$cfg_path ./apps/redis-server ./apps/redis.conf "
app_cmd="env LD_LIBRARY_PATH=. LD_PRELOAD=./interpose.so node_id=$self_id cfg_path=$cfg_path ./apps/redis-server --port 26379"
$app_cmd
