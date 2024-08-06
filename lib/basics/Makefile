CC=gcc

SOURCES=$(shell find . -name '*.c')
OBJECTS=$(SOURCES:.c=.o)

CFLAGS=-O2 -g -Wall -fPIC
LDFLAGS=-lm

all: $(OBJECTS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJECTS)
