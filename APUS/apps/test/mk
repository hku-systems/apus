#!/bin/bash

APP_DIR=`pwd`
XNRW_DIR=$APP_DIR/XNRW

pushd $XNRW_DIR
rm -rf obj
mkdir obj
g++ -v --std=c++11 -Wall -c src/ThreadPool.cpp -o obj/ThreadPool.o -Iinclude -lpthread

popd
rm -rf obj bin
mkdir obj bin
g++ -v --std=c++11 -Wall -c server.cpp -o obj/server.o -lpthread
g++ -v --std=c++11 -Wall -c client.cpp -o obj/client.o -lpthread

g++ -v --std=c++11 -Wall obj/server.o $XNRW_DIR/obj/ThreadPool.o -o bin/server -lpthread
g++ -v --std=c++11 -Wall obj/client.o $XNRW_DIR/obj/ThreadPool.o -o bin/client -lpthread
