# Make all or any of: act_storage, act_index, act_prep.

DIR_TARGET = target
DIR_OBJ = $(DIR_TARGET)/obj
DIR_BIN = $(DIR_TARGET)/bin

COMMON_SOURCES = cfg.c histogram.c queue.c random.c
INDEX_SOURCES = act_index.c cfg_index.c
STORAGE_SOURCES = act_storage.c cfg_storage.c

ACT_INDEX_SOURCES = $(COMMON_SOURCES:%=common/%) $(INDEX_SOURCES:%=index/%)
ACT_STORAGE_SOURCES = $(COMMON_SOURCES:%=common/%) $(STORAGE_SOURCES:%=storage/%)
ACT_PREP_SOURCES = prep/act_prep.c common/random.c

INDEX_OBJECTS = $(ACT_INDEX_SOURCES:%.c=$(DIR_OBJ)/%.o)
STORAGE_OBJECTS = $(ACT_STORAGE_SOURCES:%.c=$(DIR_OBJ)/%.o)
PREP_OBJECTS = $(ACT_PREP_SOURCES:%.c=$(DIR_OBJ)/%.o)

INDEX_BINARY = $(DIR_BIN)/act_index
STORAGE_BINARY = $(DIR_BIN)/act_storage
PREP_BINARY = $(DIR_BIN)/act_prep

ALL_OBJECTS = $(INDEX_OBJECTS) $(STORAGE_OBJECTS) $(PREP_OBJECTS)
ALL_DEPENDENCIES = $(ALL_OBJECTS:%.o=%.d)

CC = gcc
CFLAGS = -g -fno-common -std=gnu99 -Wall -D_REENTRANT -D_FILE_OFFSET_BITS=64
CFLAGS += -MMD
LDFLAGS = $(CFLAGS)
INCLUDES = -I. -I/usr/include
LIBRARIES = -lpthread -lrt

default: all

all: act_index act_storage act_prep

target_dir:
	/bin/mkdir -p $(DIR_BIN) \
		$(DIR_OBJ)/common $(DIR_OBJ)/index $(DIR_OBJ)/storage $(DIR_OBJ)/prep

act_index: target_dir $(INDEX_OBJECTS)
	echo "Linking $@"
	$(CC) $(LDFLAGS) -o $(INDEX_BINARY) $(INDEX_OBJECTS) $(LIBRARIES)

act_storage: target_dir $(STORAGE_OBJECTS)
	echo "Linking $@"
	$(CC) $(LDFLAGS) -o $(STORAGE_BINARY) $(STORAGE_OBJECTS) $(LIBRARIES)

act_prep: target_dir $(PREP_OBJECTS)
	echo "Linking $@"
	$(CC) $(LDFLAGS) -o $(PREP_BINARY) $(PREP_OBJECTS) $(LIBRARIES)

# For now we only clean everything.
clean:
	/bin/rm -rf $(DIR_TARGET)

-include $(ALL_DEPENDENCIES)

$(DIR_OBJ)/%.o:  %.c
	echo "Building $@"
	$(CC) $(CFLAGS)  -o $@ -c $(INCLUDES) $<
