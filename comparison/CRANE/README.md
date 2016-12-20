### CRANE

#### Build Crane

Build crane by the command below

```
./mk
```



#### Run Crane

For Relica

```
# run proxy
cd ./crane/server.out -n $relica_id -r -m r -c ./crane/nodes.local.cfg -l ./crane/log
# run redis
./crane/redis/src/redis-server --port $run_port
```

Run Primary

```
# run proxy
cd ./crane;./server.out -n $relica_id -r -m s -c ./nodes.local.cfg -l ./log
# run redis
./crane/redis/src/redis-server --port $run_port
```

Run Client

```
export LD_PRELOAD=~/crane/libevent_paxos/client-ld-preload/libclilib.so 
./crane/redis/src/redis-benchmark -h $primary_ip -p 9000 -c $client_num -n $total_request -d request_size -t set
```

