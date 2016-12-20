#!/bin/sh 

set -x

ps -elf | grep redis

self_id=`cat node_id`
cfg_path=./nodes.local.cfg
app_pid=`ps -ef | grep  redis-server | grep -v grep | awk '{print $2}'`
ls -la /proc/$app_pid/fd

lsof -p $app_pid
#python guard.py node_id pid rdma.cfg

python guard.py $self_id $app_pid $cfg_path
