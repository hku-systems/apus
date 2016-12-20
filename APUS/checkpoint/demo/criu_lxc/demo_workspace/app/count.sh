#!/bin/sh
cnt=0
while :; do
    echo "count is $cnt"
    sleep 1
    cnt=`expr $cnt + 1`
done


