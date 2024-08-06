all: build

build:
	gcc main.c -I$(LPSP_ROOT)/lib -L$(LPSP_ROOT)/lib -o main -O2 -g -llpsp -pthread -D NUM_TRIPWIRES=$(wires)

clean:
	rm -f main
