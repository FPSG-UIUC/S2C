all: build

NUM_SAMPLES ?= 1000

build:
	make -C ./aes
	gcc main.c -I$(LPSP_ROOT)/lib  -L$(LPSP_ROOT)/lib -L./aes -Wall -Wl,-rpath=./aes -o main -O2 -g -llpsp -laes -ldl -DNUM_ENCRYPTION=$(NUM_SAMPLES)

clean:
	rm -f main




