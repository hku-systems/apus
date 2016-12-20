#!/bin/sh

sudo lxc-stop -n u1
sleep 1
sudo tar -xvf  dump_demo_workspace.tgz -C /var/lib/lxc/u1/rootfs/home/ubuntu/
