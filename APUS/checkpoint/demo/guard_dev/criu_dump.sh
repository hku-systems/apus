#!/bin/sh

if [ -d "/dump" ]; then
	rm -rf dump
fi

mkdir -p dump

sudo /sbin/criu dump  -v4 -D ./dump -t $1
