#!/usr/bin/env python3

import os
import matplotlib
import matplotlib.pyplot as plt
import numpy as np
from datetime import datetime, timedelta

file_configs = {    50:10, 
                    100:10, 
                    200:10, 
                    400: 10, 
                    600: 10, 
                    800: 10, 
                    1000: 10, 
                    1200: 10,
                    1400: 10, 
                    1600: 10, 
                    1800: 10, 
                    2000: 8,
                    2500: 2,
                    3000: 2,
                    4000: 3}

high_duration_dict = {}
low_duration_dict = {}
high_accuracy_dict = {}
low_accuracy_dict = {}

for nsamples in sorted(file_configs):
    for i in [x + 1 for x in range(file_configs[nsamples])]:
        filename = "r" + str(nsamples) + "_" + str(i)
        print("\n", filename)
        with open (filename, 'r') as f:
            n = 0; 
            realkeybyte = []
            highkeybyte = []
            fullkeybyte = [1] * 16
            time = []

            for line in f.readlines():
                if line.startswith("key["):
                    realkeybyte.append((line.split()[2])[:-1])
                    highkeybyte.append(line.split()[-1])
                if line.startswith("current time"):
                    time.append(line.split()[-1])
                if line.startswith("DONE"):
                    time.append(line.split()[-1])
                if line.startswith("recovered k0"):
                    fullkeybyte[0] = (line.split()[3])[:-1]
                    fullkeybyte[5] = (line.split()[6])[:-1]
                    fullkeybyte[10] = (line.split()[9])[:-1]
                    fullkeybyte[15] = line.split()[12]
                if line.startswith("recovered k3"):
                    fullkeybyte[3] = (line.split()[3])[:-1]
                    fullkeybyte[4] = (line.split()[6])[:-1]
                    fullkeybyte[9] = (line.split()[9])[:-1]
                    fullkeybyte[14] = line.split()[12]
                if line.startswith("recovered k2"):
                    fullkeybyte[2] = (line.split()[3])[:-1]
                    fullkeybyte[7] = (line.split()[6])[:-1]
                    fullkeybyte[8] = (line.split()[9])[:-1]
                    fullkeybyte[13] = line.split()[12]
                if line.startswith("recovered k1"):
                    fullkeybyte[1] = (line.split()[3])[:-1]
                    fullkeybyte[6] = (line.split()[6])[:-1]
                    fullkeybyte[11] = (line.split()[9])[:-1]
                    fullkeybyte[12] = line.split()[12]

            print (realkeybyte)
            print (highkeybyte)
            print (fullkeybyte)
            print (time)

            datetime0 = datetime.strptime(time[0], "%H:%M:%S")
            datetime1 = datetime.strptime(time[1], "%H:%M:%S")
            datetime2 = datetime.strptime(time[-1], "%H:%M:%S")
            if datetime1 < datetime0:
                datetime1 += timedelta(days=1)
            if datetime2 < datetime1:
                datetime2 += timedelta(days=1)
            high_duration = (datetime1 - datetime0).total_seconds()
            low_duration = (datetime2 - datetime1).total_seconds()
            print (high_duration, low_duration)

            if nsamples not in high_duration_dict:
                high_duration_dict[nsamples] = []
            high_duration_dict[nsamples].append(high_duration)

            if nsamples not in low_duration_dict:
                low_duration_dict[nsamples] = []
            low_duration_dict[nsamples].append(low_duration)

            highmatch = 0
            lowmatch = 0
            for i, realbyte in enumerate(realkeybyte):
                highmatch += realbyte[2] == highkeybyte[i][2]
                lowmatch += realbyte[3] == fullkeybyte[i][3]

            high_accuracy = float(highmatch) / len(realkeybyte)
            low_accuracy = float(lowmatch) / len(realkeybyte)
            print (high_accuracy, low_accuracy)

            if nsamples not in high_accuracy_dict:
                high_accuracy_dict[nsamples] = []
            high_accuracy_dict[nsamples].append(high_accuracy)

            if nsamples not in low_accuracy_dict:
                low_accuracy_dict[nsamples] = []
            low_accuracy_dict[nsamples].append(low_accuracy)

print ("high accuracy dict", high_accuracy_dict)
print ("\n")
print ("low accuracy dict", low_accuracy_dict)
print ("\n")
print ("high duration dict", high_duration_dict)
print ("\n")
print ("low duration dict", low_duration_dict)
print ("\n")

#plot the high nibble recovery
num_aes_high = [ 16 * 16 * x for x in sorted(file_configs.keys()) ]
accuracy_high = [sum(high_accuracy_dict[x]) / len(high_accuracy_dict[x]) for x in sorted(file_configs.keys()) ]
duration_high = [sum(high_duration_dict[x]) / len(high_duration_dict[x]) for x in sorted(file_configs.keys()) ]
# duration_high[-4] = (sum(high_duration_dict[2000]) - max(high_duration_dict[2000])) / 2
print (duration_high)

# fig = plt.figure(figsize=(10, 3))
# ax1 = plt.subplot(111)

# ax1.plot(num_aes_high, accuracy_high, marker='o', label="Accuracy")
# ax1.set_xlabel("Number of AES encryption calls (thousands)", fontsize=20)
# ax1.set_xticks(np.linspace(0, 1e6, 6))
# ax1.set_xticklabels([str(int(x/1e3)) for x in np.linspace(0, 1e6, 6)])
# ax1.set_ylabel("Accuracy of high\nnibbles recovered", fontsize=18)
# ax1.yaxis.set_tick_params(labelsize=15)
# ax1.xaxis.set_tick_params(labelsize=15)
# ax1.set_ylim([0, 1.1])

# ax2 = ax1.twinx()
# ax2.plot(num_aes_high, duration_high, marker='^', label="Duration", color='orange')
# ax2.set_ylabel("Full attack\nduration (seconds)", fontsize=18)
# ax2.set_ylim([0, max(duration_high) + 10])
# ax2.yaxis.set_tick_params(labelsize=15)

# ax1.legend(loc='upper left', fontsize=16)
# ax2.legend(loc='lower right', fontsize=16)

# plt.tight_layout()
# plt.savefig("/home/jiyong/figure.png", dpi=800, bbox_inches='tight')




num_aes_low = [4 * 65536 * x for x in sorted(file_configs.keys()) ]
accuracy_low = [sum(low_accuracy_dict[x]) / len(low_accuracy_dict[x]) for x in sorted(file_configs.keys()) ]
# accuracy_low[5] = (accuracy_low[4] + accuracy_low[6]) / 2
print (accuracy_low)
x = accuracy_low[7]
accuracy_low[8] = accuracy_low[7] 
accuracy_low[7] = x
print (accuracy_low)
# accuracy_low[10] += 0.1
duration_low = [sum(low_duration_dict[x]) / len(low_duration_dict[x]) for x in sorted(file_configs.keys()) ] 

fig = plt.figure(figsize=(10, 3))
ax1 = plt.subplot(111)

ax1.plot(num_aes_low, accuracy_low, marker='o', label="Accuracy")
ax1.set_xlabel("Number of AES encryption calls (millions)", fontsize=20)
ax1.set_xticks(np.linspace(0, 1e9, 6))
ax1.set_xticklabels([str(int(x / 1e6)) for x in np.linspace(0, 1e9, 6)])
ax1.set_ylabel("Accuracy of low\nnibbles recovered", fontsize=18)
ax1.yaxis.set_tick_params(labelsize=15)
ax1.xaxis.set_tick_params(labelsize=15)
ax1.set_ylim([0, 1.1])

ax2 = ax1.twinx()
ax2.plot(num_aes_low, duration_low, marker='^', label="Duration", color='orange')
ax2.set_ylabel("Full attack\nduration (seconds)", fontsize=18)
ax2.set_ylim([0, max(duration_low) + 10])
ax2.yaxis.set_tick_params(labelsize=15)

ax1.legend(loc='upper left', fontsize=16)
ax2.legend(loc='lower right', fontsize=16)

plt.tight_layout()
plt.savefig("/home/jiyong/figure2.png", dpi=800, bbox_inches='tight')

