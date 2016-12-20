#!/bin/sh

AIM_IP="147.8.178.252"

# tar the demo_workspace

sudo tar -zcvf dump_demo_workspace.tgz -C /var/lib/lxc/u1/rootfs/home/ubuntu/ demo_workspace
scp ./dump_demo_workspace.tgz admin@$AIM_IP:/tmp/

