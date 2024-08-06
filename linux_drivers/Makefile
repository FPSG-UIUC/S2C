obj-m += pmc.o
KERNEL_NAME := $(MAKE) -C /lib/modules/$(shell uname -r)/build M=$(shell pwd)/

all: build install

build:
	$(KERNEL_NAME) modules

install:
	sudo insmod pmc.ko
	#sudo modprobe pmc.ko --force-vermagic

uninstall:
	sudo rmmod pmc.ko

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c *.tmp_versions *.mod Module.symvers modules.order
