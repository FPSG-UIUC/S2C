EXPERIMENT_DIR=$(shell pwd)/experiments
LIB_DIR=$(shell pwd)/lib
DATABASE_DIR=$(shell pwd)/database

EXPERIMENT_SUBDIRS=$(shell find $(EXPERIMENT_DIR) -name "expr*")

BUILD_EXPERIMENTS=$(EXPERIMENT_SUBDIRS:%=build-%)
CLEAN_EXPERIMENTS=$(EXPERIMENT_SUBDIRS:%=clean-%)

BUILD_LIB=$(LIB_DIR:%=build-%)
CLEAN_LIB=$(LIB_DIR:%=clean-%)

all: $(BUILD_LIB) $(BUILD_EXPERIMENTS)

$(BUILD_EXPERIMENTS):
	$(MAKE) -C $(@:build-%=%) curr-dir=$(curr-dir)/$(@:build-%=%) lib=$(LIB_DIR)

$(BUILD_LIB):
	$(MAKE) -C $(@:build-%=%) curr-dir=$(curr-dir)/$(@:build-%=%)

clean: $(CLEAN_EXPERIMENTS) $(CLEAN_LIB)

$(CLEAN_EXPERIMENTS):
	$(MAKE) clean -C $(@:clean-%=%) curr-dir=$(curr-dir)/$(@:clean-%=%)

$(CLEAN_LIB):
	$(MAKE) clean -C $(@:clean-%=%) curr-dir=$(curr-dir)/$(@:clean-%=%)
