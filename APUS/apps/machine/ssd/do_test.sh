#!/bin/sh


for i in `seq 0 5` ; do
	echo "test case: $i"
	/usr/bin/time -p ./test.elf $i  2>&1 | tee time.$i.log
	strace -c -T ./test.elf $i 2>&1 | tee strace.$i.log	
	ltrace -c -T ./test.elf $i 2>&1 | tee ltrace.$i.log	
done

