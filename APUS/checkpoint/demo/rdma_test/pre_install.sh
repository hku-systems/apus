#!/bin/sh
set -x
# Step.1 Dev deps from https://www.systems.ethz.ch/sites/default/files/file/acn2015/assignments/project1/assignment8.pdf
apt-get install build-essential
apt-get install libtool autoconf automake linux-tools-common
apt-get install fakeroot build-essential crash kexec-tools makedumpfile kernel-wedge
apt-get build-dep linux
apt-get install git-core libncurses5 libncurses5-dev libelf-dev
apt-get install linux-headers-$(uname -r)
# pre deps
apt-get install -y tcl8.4 chrpath tk8.4 dkms graphviz dpatch quilt libnl1 swig
apt-get install -y gfortran libgfortran3
