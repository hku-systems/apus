#!/bin/sh
set -x
tail count.log
cd dump_dir
criu restore -d -vvv -o restore.log
cat ./restore.log
cd -
tail -f count.log



