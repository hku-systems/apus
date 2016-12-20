#1/bin/sh
set -x
setsid app/count.sh < /dev/null 2>/dev/null >count.log &
sleep 1
ps -efl | grep count.sh
