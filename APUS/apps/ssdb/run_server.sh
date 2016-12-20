#!/bin/sh
set -x
# Step0. This script is aiming to launch the application server.

# Step1. Run as daemon
INS_RooT=/usr/local/ssdb
WORK_DIR=/tmp
Config_PATH=$INS_RooT/ssdb.conf
cd $WORK_DIR
sudo pkill -9 ssdb-server
sudo rm -rf $INS_RooT/var
sudo mkdir -p $INS_RooT/var

sudo $INS_RooT/ssdb-server -d $Config_PATH
sleep 3
ps -elf | grep ssdb-server
sudo netstat -nlpt
cd -
