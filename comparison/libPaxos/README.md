### LibPaxos

#### Install

Run the command to install LibPaxos



#### Run LibPaxos

Run Replica

A sample of configuration file is in the current directory

```
./libpaxos/build/sample/replica $replica_id $lib_conf
```

Run Client

```
./libpaxos/build/sample/client $replica_id $lib_conf -v $request_size -o $client_num -p 0
```



See more configuration in ...

