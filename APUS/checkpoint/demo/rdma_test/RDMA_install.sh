#!/bin/sh

# Step.2 RDMA Driver from http://www.mellanox.com/related-docs/prod_software/Mellanox_OFED_Linux_user_manual_1_5_1.pdf 
ISO_NAME=/tmp/MLNX_OFED_LINUX-3.2-2.0.0.0-ubuntu14.04-x86_64.iso
mount -o ro,loop $ISO_NAME /mnt
cd /mnt
./mlnxofedinstall --without-fw-update --all  

cat /etc/security/limits.conf

# verified
/usr/bin/hca_self_test.ofed
/etc/infiniband/info
mst start
mst status

/sbin/connectx_port_config -s

