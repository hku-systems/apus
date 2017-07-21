1. Initialize environment (on every node)
```
git clone --recursive https://github.com/hku-systems/apus.git
cd apus/APUS
export RDMA_ROOT=`pwd`
```
2. Install the prerequisite (on every node)
```
$RDMA_ROOT/RDMA/mk
cd $RDMA_ROOT/RDMA/target
make clean
make
cd $RDMA_ROOT/apps/test
make clean
make
```
3. Configure the cluster (on every node)

You only need to edit `$RDMA_ROOT/RDMA/target/nodes.local.cfg` to replace the `ip_address` in `consensus_config` field.

4. Run the test
```
cd $RDMA_ROOT/eval
./test.sh -s<message_size> <list of addresses>
```
The below example runs 3 replicas, with 1KB-message
```
./test.sh -s1024 hkucs@10.22.1.1 hkucs@10.22.1.2 hkucs@10.22.1.3
```
The test result is saved in `$RDMA_ROOT/eval/test_result.dat`.

**Note:**

1. The test script must be executed on the first node in the list.
2. The order of the list addresses must match the order of `ip_address` in `$RDMA_ROOT/RDMA/target/nodes.local.cfg` field.
