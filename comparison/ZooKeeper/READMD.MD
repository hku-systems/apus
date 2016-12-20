### ZooKeeper

#### Build ZooKeeper

Build ZooKeeper by the command below

```
./mk
```



#### Run ZooKeeper

For Relica

```
./zookeeper/bin/zkServer.sh start # start server
./zookeeper/bin/zkServer.sh status # check if the server is started
```



For Client

```
PYTHONPATH=./zk-smoketest/lib.linux-x86_64-2.6 LD_LIBRARY_PATH=./zk-smoketest/lib.linux-x86_64-2.6 ./zk-smoketest/zk-latencies.py --servers=$leader_ip":$leader_port" --znode_size=$request_size --znode_count=$request_count --synchronous --root_znode=/zk-latencies
```



