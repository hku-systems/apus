# Redis Sentinel Documentation
Redis Sentinel provides high availability for Redis. In practical terms this means that using Sentinel you can create a Redis deployment that resists without human intervention to certain kind of failures.

Redis Sentinel also provides other collateral tasks such as monitoring, notifications and acts as a configuration provider for clients.

This is the full list of Sentinel capabilities at a macroscopical level (i.e. the *big picture*):

- **Automatic failover**. If a master is not working as expected, Sentinel can start a failover process where a slave is promoted to master, the other additional slaves are reconfigured to use the new master, and the applications using the Redis server informed about the new address to use when connecting.

## Distributed nature of Sentinel

Redis Sentinel is a distributed system:

Sentinel itself is designed to run in a configuration where there are multiple Sentinel processes cooperating together. The advantage of having multiple Sentinel processes cooperating are the following:

1. Failure detection is performed when multiple Sentinels agree about the fact a given master is no longer available. This lowers the probability of false positives.

## Example 2: basic setup with three boxes

This is a very simple setup, that has the advantage to be simple to tune for additional safety. It is based on three boxes, each box running both a Redis process and a Sentinel process.

```
       +----+
       | M1 |
       | S1 |
       +----+
          |
+----+    |    +----+
| R2 |----+----| R3 |
| S2 |         | S3 |
+----+         +----+

Configuration: quorum = 2
```
If the master M1 fails, S2 and S3 will agree about the failure and will be able to authorize a failover, making clients able to continue.

In every Sentinel setup, being Redis asynchronously replicated, there is always the risk of losing some write because a given acknowledged write may not be able to reach the slave which is promoted to master. However in the above setup there is an higher risk due to clients partitioned away with an old master, like in the following picture:

```
         +----+
         | M1 |
         | S1 | <- C1 (writes will be lost)
         +----+
            |
            /
            /
+------+    |    +----+
| [M2] |----+----| R3 |
| S2   |         | S3 |
+------+         +----+
```

In this case a network partition isolated the old master M1, so the slave R2 is promoted to master. However clients, like C1, that are in the same partition as the old master, may continue to write data to the old master. This data will be lost forever since when the partition will heal, the master will be reconfigured as a slave of the new master, discarding its data set.

This problem can be mitigated using the following Redis replication feature, that allows to stop accepting writes if a master detects that is no longer able to transfer its writes to the specified number of slaves.

```
min-slaves-to-write 1
min-slaves-max-lag 10
```
