#!/bin/sh

set -x
# before dump
REDIS_CLI_1="./apps/redis-cli -h 10.22.1.1 -p 26379"
REDIS_CLI_2="./apps/redis-cli -h 10.22.1.2 -p 26379"
REDIS_CLI_3="./apps/redis-cli -h 10.22.1.3 -p 26379"

CK_NODE="1"  # 10.22.1.2
RE_NODE="2"  # 10.22.1.3

ps -elf | grep redis
echo "set name redis_1" | $REDIS_CLI_1
echo "set key 1" | $REDIS_CLI_1
echo "set name redis_2" | $REDIS_CLI_2
echo "set key 1" | $REDIS_CLI_2
echo "set name redis_3" | $REDIS_CLI_3
echo "set key 1" | $REDIS_CLI_3

echo "get name" | $REDIS_CLI_1
echo "get name" | $REDIS_CLI_2
echo "get name" | $REDIS_CLI_3

echo "checkpoint $CK_NODE 1\n" | nc -U /tmp/guard.sock
sleep 3 
echo "checkpoint $CK_NODE 2\n" | nc -U /tmp/guard.sock
sleep 3 
echo "checkpoint finished"

echo "set key 2" | $REDIS_CLI_3 
echo "set key 3" | $REDIS_CLI_3
echo "set key 4" | $REDIS_CLI_3
echo "get key" | $REDIS_CLI_3

sleep 3
echo "restore $RE_NODE 5\n" | nc -U /tmp/guard.sock
sleep 3
echo "restore finished"
echo "get name" | $REDIS_CLI_3
echo "get key" | $REDIS_CLI_3 
