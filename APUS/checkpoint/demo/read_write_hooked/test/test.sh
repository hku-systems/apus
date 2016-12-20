#!/bin/sh
set -x
LD_PRELOAD="../hooked/hooked.so" ../host/test

