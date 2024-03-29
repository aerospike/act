# Make all or any of: act_storage, act_index, act_prep.

ARCH = $(shell uname -m)

ifeq ($(ARCH), x86_64)
	CFLAGS = -march=nocona
else ifeq ($(ARCH), aarch64)
	CFLAGS = -mcpu=neoverse-n1
else
	$(error unhandled arch "$(ARCH)")
endif

DIR_TARGET = target
DIR_OBJ = $(DIR_TARGET)/obj
DIR_BIN = $(DIR_TARGET)/bin

DIR_PKG = $(DIR_TARGET)/packages
DIR_RPM = pkg/rpm/RPMS
DIR_DEB = pkg/deb/DEBS

SRC_DIRS = common index prep storage
OBJ_DIRS = $(SRC_DIRS:%=$(DIR_OBJ)/src/%)

COMMON_SRC = cfg.c hardware.c histogram.c io.c queue.c random.c trace.c
INDEX_SRC = act_index.c cfg_index.c
STORAGE_SRC = act_storage.c cfg_storage.c

INDEX_SOURCES = $(COMMON_SRC:%=src/common/%) $(INDEX_SRC:%=src/index/%)
PREP_SOURCES = $(COMMON_SRC:%=src/common/%) src/prep/act_prep.c
STORAGE_SOURCES = $(COMMON_SRC:%=src/common/%) $(STORAGE_SRC:%=src/storage/%)

INDEX_OBJECTS = $(INDEX_SOURCES:%.c=$(DIR_OBJ)/%.o)
PREP_OBJECTS = $(PREP_SOURCES:%.c=$(DIR_OBJ)/%.o)
STORAGE_OBJECTS = $(STORAGE_SOURCES:%.c=$(DIR_OBJ)/%.o)

INDEX_BINARY = $(DIR_BIN)/act_index
PREP_BINARY = $(DIR_BIN)/act_prep
STORAGE_BINARY = $(DIR_BIN)/act_storage

ALL_OBJECTS = $(INDEX_OBJECTS) $(PREP_OBJECTS) $(STORAGE_OBJECTS)
ALL_DEPENDENCIES = $(ALL_OBJECTS:%.o=%.d)

CFLAGS += -g -fno-common -std=gnu99 -Wall -D_REENTRANT -D_FILE_OFFSET_BITS=64
CFLAGS += -D_GNU_SOURCE -MMD
LDFLAGS = $(CFLAGS)
INCLUDES = -Isrc -I/usr/include
LIBRARIES = -lpthread -lrt

default: all

all: act_index act_prep act_storage

target_dir:
	/bin/mkdir -p $(DIR_BIN) $(OBJ_DIRS) $(DIR_PKG)

act_index: target_dir $(INDEX_OBJECTS)
	echo "Linking $@"
	$(CC) $(LDFLAGS) -o $(INDEX_BINARY) $(INDEX_OBJECTS) $(LIBRARIES)

act_prep: target_dir $(PREP_OBJECTS)
	echo "Linking $@"
	$(CC) $(LDFLAGS) -o $(PREP_BINARY) $(PREP_OBJECTS) $(LIBRARIES)

act_storage: target_dir $(STORAGE_OBJECTS)
	echo "Linking $@"
	$(CC) $(LDFLAGS) -o $(STORAGE_BINARY) $(STORAGE_OBJECTS) $(LIBRARIES)

-include $(ALL_DEPENDENCIES)

$(DIR_OBJ)/%.o: %.c
	echo "Building $@"
	$(CC) $(CFLAGS) -o $@ -c $(INCLUDES) $<

.PHONY: rpm
rpm:
	$(MAKE) -f pkg/Makefile.rpm

.PHONY: deb
deb:
	$(MAKE) -f pkg/Makefile.deb

# For now we only clean everything.
.PHONY: clean
clean:
	/bin/rm -rf $(DIR_TARGET)
	/bin/rm -rf $(DIR_RPM)
	/bin/rm -rf $(DIR_DEB)
	/bin/rm -rf dist
