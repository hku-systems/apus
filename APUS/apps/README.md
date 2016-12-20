# How to add a testing task

## Introduction
This folder contains several famous Linux server applications testing tasks, such as Redis, Zookeepers and memcached. Our project is aiming to provide a high availability solution for these applications through state machine replication. Two direct objectives for these testing tasks are:

1) To obtain the real performance statistic when the application is running. 

2) To test the performance overhead when our solution is working. 

In the first stage, we are aiming to get performance statistic though running standard testing benchmark.

In order to do testing effectively and efficiently, I did SSDB test, and have written some shell scripts for installation, running, testing and result analysis. The following sections will introduce each part of a testing job.

## Installation
A script named build.sh should be implemented to download source code and compile, then install the binary to a location.

## Running and Stopping
Script named run_server.sh and stop_server.sh should be implemented to run and stop server.

## Testing
A script named test.sh should be implemented to run the standard testing benchmark. A file named result.txt will be the output of testing.

## Analysing
Because the testing outputs are not standardised, we can write some analysis tools to show the result more clearly. For example, the result of SSDB shows query per second (QPS), but dose not show how much time one request is used. A analyse.sh can be implemented to do more research. A file named detail.txt will be the detailed result.

## Result

Once the testing task is finished, please write a brief document named README.md to describe the testing task, including the result. For example, in order to verify the result, we can cite other testing result from different sources.

## Testing Environment
I did some preliminary testing for the performance of testing machine(project server), such as CPU, Memory and Disk. If the testing environment has been changed, Please redo the environment test. Please run Machine/machine.sh.

## Conclusion

The document introduced how to add a testing task for our project.