#!/bin/sh

sudo lxc-stop -n u1
sleep 1
sudo cp -ra demo_workspace /var/lib/lxc/u1/rootfs/home/ubuntu/
