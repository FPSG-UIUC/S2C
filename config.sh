#!/bin/sh

rm -rf configure
touch configure

root=$(pwd)
echo "export LPSP_ROOT=$root" >> ./configure
echo "export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:$root/lib" >> ./configure
echo "export DYLD_LIBRARY_PATH=\$DYLD_LIBRARY_PATH:$root/lib" >> ./configure
