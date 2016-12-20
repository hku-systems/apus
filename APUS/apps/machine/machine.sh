#!/bin/sh

cat /proc/cpuinfo

cat /proc/meminfo

uname -a

sudo smartctl -a  /dev/sda

dd if=/dev/zero of=/tmp/output bs=8k count=20k
dd if=/dev/zero of=/tmp/output bs=8k count=20k
dd if=/dev/zero of=/tmp/output bs=8k count=20k
