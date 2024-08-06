#!/usr/bin/env python3

import os
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as mtick

size_list = []
lock1_list = []
lock2_list = []
lock3_list = []
lock4_list = []

with open ("./out", 'r') as f:
    for line in f.readlines():
        if "locked" in line:
            num_autolocked = int(line.split()[0])
            evset_size = int(line.split()[3])
            duration = int(line.split()[-1])
            if num_autolocked == 1:
                size_list.append(evset_size)
                lock1_list.append(duration);
            elif num_autolocked == 2:
                lock2_list.append(duration)
            elif num_autolocked == 3:
                lock3_list.append(duration)
            elif num_autolocked == 4:
                lock4_list.append(duration)

print(lock1_list)
print(lock2_list)
print(lock3_list)
print(lock4_list)


fig = plt.figure(figsize=(7,2.7))
ax = plt.subplot(111)
ax.plot(size_list, lock1_list, marker = 'o', markersize = 8, label="Size of T = 1");
ax.plot(size_list, lock2_list, marker = 's', markersize = 8, label="Size of T = 2");
ax.plot(size_list, lock3_list, marker = 'x', markersize = 8, label="Size of T = 3");
ax.plot(size_list, lock4_list, marker = '^', markersize = 8, label="Size of T = 4");

ax.set_xticks(size_list)
ax.set_xticklabels([str(x) for x in size_list])
ax.axvline(x=8, color='gray', linestyle=':')
ax.axvline(x=9, color='gray', linestyle=':')
ax.axvline(x=10, color='gray', linestyle=':')
ax.axvline(x=11, color='gray', linestyle=':')
ax.xaxis.set_tick_params(labelsize=14)
ax.yaxis.set_tick_params(labelsize=14)
ax.set_xlabel("Size of S", fontsize=17);
ax.set_ylabel("Cycles spent by \ncore 2's traversal", fontsize=16)

ax.legend(fontsize=16, loc='center left', bbox_to_anchor=(1,0.5))
plt.savefig("figure.png", dpi=1200, bbox_inches='tight')



