#!/bin/sh
set -x
KILL_CMD='sudo pkill -9 -f redis-server'
ssh hkucs-PowerEdge-R430-1 $KILL_CMD 
ssh hkucs-PowerEdge-R430-2 $KILL_CMD
ssh hkucs-PowerEdge-R430-3 $KILL_CMD

sh ./check_redis_all.sh 
