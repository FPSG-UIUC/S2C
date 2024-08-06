#!/bin/bash

# enable huge pages
# you can run "grep HugePages /proc/meminfo" to check if huge page is correctly configured
sudo sysctl -w vm.nr_hugepages=82

# unlimited stack size to avoid segfault
ulimit -s unlimited

# drivers for accessing timer from userspace
sudo make -C ./linux_drivers

