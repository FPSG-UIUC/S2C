CC=gcc
AR=ar

BASIC_DIR=./basics
EVSET_DIR=./eviction_set
PPVAR_DIR=./prime_probe_variants

SUBDIRS=$(BASIC_DIR) $(EVSET_DIR) $(PPVAR_DIR)

BUILD_SUBDIRS=$(SUBDIRS:%=build-%)
CLEAN_SUBDIRS=$(SUBDIRS:%=clean-%)

OBJECTS=$(shell find . -name '*.o')
SHARED_LIB=liblpsp.so
STATIC_LIB=liblpsp.a

LDFLAGS=-lm -lpthread

all: $(BUILD_SUBDIRS)
	$(CC) $(LDFLAGS) -shared -o $(SHARED_LIB) $(OBJECTS)
	$(AR) rcs $(STATIC_LIB) $(OBJECTS)


$(BUILD_SUBDIRS):
	$(MAKE) -C $(@:build-%=%)

clean: $(CLEAN_SUBDIRS)
	rm -rf $(SHARED_LIB)

$(CLEAN_SUBDIRS):
	$(MAKE) clean -C $(@:clean-%=%)





