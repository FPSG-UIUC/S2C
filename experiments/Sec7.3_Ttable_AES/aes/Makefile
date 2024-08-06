all: build_lib build_test


build_lib:
	gcc -Wall -fPIC -c aes_core.c
	gcc -shared -o libaes.so aes_core.o

build_test:
	gcc -Wall -Wl,-rpath=./ -o test aes_test.c -L./ -laes -g -ldl

clean:
	rm *.o
