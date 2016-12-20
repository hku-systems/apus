#!/bin/sh

# reload driver according previous step.
/etc/init.d/openibd restart
# Step.2 from https://community.mellanox.com/docs/DOC-2086
apt-get install libmlx4-1 infiniband-diags ibutils ibverbs-utils rdmacm-utils perftest
apt-get install tgt
apt-get install targetcli
apt-get install open-iscsi-utils open-iscsi
