#!/bin/bash
if [ -f ~/.bashrc ]; then
  source ~/.bashrc
fi
#information in '' will direct send to remote while "" will parse in local first
#ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/remote_host1) "mkdir -p $RDMA_ROOT"
#scp -r $RDMA_ROOT $LOGNAME@$(cat $RDMA_ROOT/apps/env/remote_host1):$RDMA_ROOT/..
#ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/remote_host1) "cd $RDMA_ROOT/apps/env && ./local_env.sh"
#ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/remote_host1) 'source ~/.bashrc'

#ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/remote_host2) "mkdir -p $RDMA_ROOT"
#scp -r $RDMA_ROOT $LOGNAME@$(cat $RDMA_ROOT/apps/env/remote_host2):$RDMA_ROOT/..
#ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/remote_host2) "cd $RDMA_ROOT/apps/env && ./local_env.sh"
#ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/remote_host2) 'source ~/.bashrc'

ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/test_host) "mkdir -p $RDMA_ROOT"
scp -r $RDMA_ROOT $LOGNAME@$(cat $RDMA_ROOT/apps/env/test_host):$RDMA_ROOT/..
ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/test_host) "cd $RDMA_ROOT/apps/env && ./local_env.sh"
ssh $LOGNAME@$(cat $RDMA_ROOT/apps/env/test_host) 'source ~/.bashrc'


cat $RDMA_ROOT/apps/env/remote_hosts | while read line
do
    ssh -f $LOGNAME@${line} "mkdir -p $RDMA_ROOT"
    scp -r $RDMA_ROOT $LOGNAME@${line}:$RDMA_ROOT/..
    ssh -f $LOGNAME@${line} "cd $RDMA_ROOT/apps/env && ./local_env.sh"
    ssh -f $LOGNAME@${line} "source ~/.bashrc"
done

