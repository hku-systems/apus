#!/bin/sh

tail count.log
rm -rf dump_dir
mkdir -p dump_dir
cd dump_dir
criu dump -t $1 -vvv -o dump.log
cat dump.log
cd -

tar -czvf dump_dir.tgz ./dump_dir


