#!/bin/sh

set -x
# before dump
REDIS_CLI_1="./apps/redis-cli -h 10.22.1.1 -p 7004"
REDIS_CLI_2="./apps/redis-cli -h 10.22.1.2 -p 7004"
REDIS_CLI_3="./apps/redis-cli -h 10.22.1.3 -p 7004"

CK_NODE="1"  # 10.22.1.2
RE_NODE="2"  # 10.22.1.3

ps -elf | grep redis
#echo "set key 1" | $REDIS_CLI_1
#echo "get key" | $REDIS_CLI_1
# send a command to RDMA to disconnect (only used for debug)
# echo "disconnect" | nc -U /tmp/checkpoint.server.sock


echo "This is a simple test, just checkpoint and resotre at No.2"
echo "checkpoint $CK_NODE 1\n" | nc -U /tmp/guard.sock
echo "checkpoint finished"
sleep 3
echo "restore $CK_NODE 2\n" | nc -U /tmp/guard.sock
echo "restore finished"
sleep 3
