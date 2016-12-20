#!/bin/sh

sudo lxc-start -n u1
sleep 3
echo "press ctrl+a q to exit lxc container"
sudo lxc-console -n u1 -t 0
