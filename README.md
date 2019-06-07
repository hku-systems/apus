# APUS: fast and scalable paxos on RDMA

Build (Ubuntu Linux 15.04)
----

The source code of APUS is based on DARE [HPDC'15]
### Dependencies
Install libev, libconfig, libdb, libibverbs:
```
sudo apt-get install libev-dev libconfig-dev libdb-dev
```
### Build APUS
Set env vars:
```
export PAXOS_ROOT=<absolute path of RDMA-PAXOS>
```
To perform a default build execute the following:
```
cd target
make clean; make
```
Run examples
----

### Run APUS with Redis

Install Redis:
```
cd apps/redis
./mk
```
Run APUS with Redis:
```
cd benchmarks
./run.sh --app=redis
```

Contact
----

Please send emails to Wang Cheng (wangch.will@gmail.com) If you encounter any problems.
