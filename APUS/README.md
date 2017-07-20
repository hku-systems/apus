1. Initialize environment (on every node)
```
git clone https://github.com/hku-systems/apus.git
cd apus/APUS
export RDMA_ROOT=`pwd`
```
2. Install the prerequisite (on every node)
```
$RDMA_ROOT/RDMA/mk
cd $RDMA_ROOT/apps/test
./mk
```
3. Configure the cluster

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

**Note:** The test script must be executed on the first node in the list.
