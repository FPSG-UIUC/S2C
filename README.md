# Synchronization Storage Channels (S2C): Timer-less Cache Side-Channel Attacks on the Apple M1 via Hardware Synchronization Instructions

## Introduction

Synchronization Storage Channel (S2C) the first cache side-channel attack that replaces the requirement of hardware timer with load-linked/store-conditional instructions (or load-exclusive/store-exclusive in ARM64 architecture). 
The attack achieves same-/cross-core cache side-channel attacks on Apple M1 (other Apple M1/M2 processors will also be tested in the future).

## Contributions of the Attack

The main contributions of this work (for now) are:
- Find out that store-conditional (stxr in ARMv8) instruction not only returns false when the data is overwritten after load-linked, but also when the **data is evicted from L1 data cache**. This observation leads to eviction-based cache side-channel (e.g., Prime+Probe) **without relying on the hardware timer** to differentiate eviction vs. no-eviction.
- Reverse-engineer **L2 cache set index mapping** of Apple M1 processors. This is necessary for constructing L2 eviction set for a target victim address.
- Construct the first timer-less cross-core cache side-channel attack on **Apple M1 processors**, and the first timer-less cross-core cache side-channel attack that exploits load-linked/store-conditional instructions, and show two Proof-of-Concepts: cross-core covert-channel, cross-process side-channel attack against T-Table AES.

## Source Code Overview

The repository is orgnanized as follows:
```
.
|-- database: pre-computed huge page offset values. Used for generating L2 eviction set for an arbitrary victim address
|-- experiments: all experiments (including basic experiments and paper experiments)
|-- lib: included libraries 
|-- linux_drivers: linux kernel driver to enable userspace access to PMC0 (high-precision timer)
```

## System Requirement

### Processors

So far we have only tested the attack on Apple M1 (including Macbook Pro and Mac Mini with M1 processors). We will test other M1 processors (M1 Pro/Max/Ultra) and M2 processors in the future. You can run `system_profiler SPHardwareDataType` to check your CPU.

### Operating System

To run all the experiments, you need to install Linux on your Mac, which is actually much easier than you thought since we have Asahi Linux now. Below shows you how to do this.

If you only want to run experiments on your MacOS, you can only run expr00-05 for now. We currently do not support cross-core attack on MacOS.

## Run S2C in Linux (Recommended)

### Step 1: Install Asahi Linux

For now, S2C attack only supported on Linux, and we have only tested the attack on Asahi Linux. You can install Asahi Linux on your mac (this [video](https://www.youtube.com/watch?v=lcmmwugTF3U) provides a nice tutorial on how to do it. If later on you want to uninstall Asahi Linux, check out this [video](https://www.youtube.com/watch?v=nMnWTq2H-N0)). 


### Step 2: Compile the build directory for compiling Linux kernel module. You can skip this step if you only want to test the attack PoC (expr12-expr14).

Do make sure you did `sudo Pacman -Syu` which installs the most recent Asahi Linux kernel version (you can find the most recent Asahi Linux packages [here](https://cdn.asahilinux.org/aarch64/asahi/)). Also make sure to reboot your device after the installation. If everything works fine,  you should find a folder under /lib/modules with its name matching `uname -r`, which is the most recent Asahi Linux version (for example, it’s `6.1.0-rc8-asahi-3-1-ARCH` when I tested it). However you won’t see a build folder there which we need for building linux kernel module, without which you cannot build your Linux kernel module. Next, you need to do the following:

1. Install the build folder: 
```
$ sudo pacman -S linux-asahi-headers
```
Now you should find `/lib/modules/YOUR_KERNEL_VERSION/build`. However, the Makefile inside is unusable so we need to get the correct Makefile. I did it in a dumb way, which is explained next.

2. Download the Asahi Linux source code with the correct version.
The best way is to go to [Asahi Linux's github repo](https://github.com/AsahiLinux/linux/tags) and find the version matching your kernel version. For example `uname -r` showed `6.1.0-rc8-asahi-3-1-ARCH` on my machine so I downloaded `asahi-6.1-rc8-3`. Uncompress the tarball after your download it. Now you should find a `localversion.05-asahi` at the top level of the folder. Delete the `-asahi` in this file, otherwise you will get a unmatched version magic and your built linux kernel module cannot be executed.

3. Overwrite the Makefile under the build folder. Do
```
$ cd /lib/modules/YOUR_KERNEL_VERSION/build // YOUR_KERNEL_VERSION is the same as the output of ‘uname -r’
$ sudo vim Makefile
```
Then delete all lines and add the following code, which points to the location of the Makefile from your downloaded source code:
```
-include /home/YOUR_USER_ID/PATH_TO_YOUR_ASAHI_LINUX_SOURCE/Makefile
```

4. Compile the build folder.
```
$ sudo make -j 4
```
Now the linux kernel module building environment is done.

### Step 3: Clone this repo onto your Linux
```
$ git clone https://github.com/FPSG-UIUC/S2C.git
```
### Step 4: Install necessary packages
```
$ sudo pacman -S openssl python3 gcc
```
### Step 5: Switch to command-line Linux for less noise (optional)

Switch to command-line mode by entering `systemctl set-default multi-user.target`. This will avoid running most processes and get you a clean, close-to-idle environment. You can switch back to graphical mode from command-line mode by 'systemctl set-default graphical.target'.

### Step 6: Set up the environment for S2C and compile
Generate the configure file, set up the environmental variables, build and install the kernel driver, and compile all libraries + experiment binaries. You need to do these steps everytime you start up your machine.
```
$ cd /PATH_TO_S2C
$ ./config.sh 
$ source ./configure
$ ./init_linux.sh 
$ make
```
If you skipped Step 2 above, `./init_linux.sh` will return error because you cannot build the linux kernel module. This kernel module is required for accessing PMC0 from userspace, which is required by some experiments for testing purposes. However, our attack PoCs do not require this driver.

Now you are ready to run the experiments!

### Step 7: Run experiments
We include some basic experiments as well as all experiments in the paper in `/experiments`.
To run an experiment, for example, expr03 (which shows that STXR can fail when the previous ldxr data is evicted from L1):
```
$ cd $LPSP_ROOT/experiments/expr03_stxr_fail_on_l1_evict
$ ./main
```

## Run S2C in MacOS

### Step 1: Install Cargo

The kernel extension is written in Rust. So install Cargo if you have not done yet:

```
$ curl https://sh.rustup.rs -sSf | sh
```

### Step 2: Set up the environment
```
$ cd /PATH_TO_S2C
$ ./config.sh // generate configure file
$ source ./configure  // setup environmental variables
$ ./init_macos.sh // build and install PMC kernel extension
```
### Step 3: Compile and Run the experiments
Notice that you can only run expr00-exor05 in MacOS.
```
$ cd $LPSP_ROOT/lib
$ make
$ cd $LPSP_ROOT/experiments/exprXX_...
$ make
$ ./main
```

## Common Errors and Fixes:

1. ... fatal error: basics/defs.h: No such file or directory

You probably forgot to do `source configure`.

2. ... segmentation fault ...

You probably forgot to do `./init_linux.sh`, such that `ulimit -t unlimited` is not set. Please run `ulimit -t unlimited`.
