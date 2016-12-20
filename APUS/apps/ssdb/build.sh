#!/bin/sh

# Step0. describe the information about the software
# Home Page: https://github.com/ideawu/ssdb/
# build instructions: http://ssdb.io/docs/install.html

# Step1. define installation path.
# Some application did not support installation path, which is fine.
INS_RooT=/usr/local/ssdb

# Step2. compile and build software at /tmp.
Build_Root=/tmp/ssdb
mkdir -p $Build_Root
cd $Build_Root
pwd

wget --no-check-certificate https://github.com/ideawu/ssdb/archive/master.zip
unzip master
cd ssdb-master
make
# optional, install ssdb in /usr/local/ssdb
sudo make install prefix=$INS_RooT
cd -
echo "Build finished."

