#!/bin/sh
# To install lxc daily

sudo apt-get install software-properties-common
sudo add-apt-repository ppa:ubuntu-lxc/daily
sudo apt-get update
sudo apt-get install lxc
