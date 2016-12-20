# RDMA installation
## env info

## pre install
sh pre_install.sh | tee -a install.log
## RDMA Linux package 
sh RDMA_install.sh | tee -a install.log
## post install
sh post_install.sh | tee -a install.log

## Verify
/usr/bin/hca_self_test.ofed




