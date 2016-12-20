#!/bin/sh

PS_CMD="ps -elf| grep redis-server | grep -v python"
ssh hkucs-PowerEdge-R430-1 $PS_CMD 
ssh hkucs-PowerEdge-R430-2 $PS_CMD 
ssh hkucs-PowerEdge-R430-3 $PS_CMD 

