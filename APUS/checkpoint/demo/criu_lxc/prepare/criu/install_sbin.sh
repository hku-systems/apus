#!/bin/sh

# To install criu binary directly

sudo cp out/lib/libprotobuf-c.so.0 out/lib/libnl-3.so.200 /lib/x86_64-linux-gnu/
sudo cp out/sbin/criu /sbin
sudo chmod u+s /sbin/criu

