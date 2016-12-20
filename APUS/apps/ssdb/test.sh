#!/bin/sh

INS_RooT=/usr/local/ssdb 
Result=result.txt
$INS_RooT/ssdb-bench 127.0.0.1 8888 10000 50 2>&1 | tee $Result
