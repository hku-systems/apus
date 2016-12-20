#!/bin/sh

set -x
# Step.1 hardware info

dmesg | grep Mellanox
lsmod | grep mlx4
lspci | grep Mellanox

# Step.2 Machine info
cat /etc/issue
uname -a
cat /proc/cpuinfo
ifconfig -a
ethtool -i eth4
df -h
mount

# Step.3 eth info lossless network
ethtool -a eth4


