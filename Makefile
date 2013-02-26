
DIR_INCLUDE = .
DIR_OBJECT = .
DIR_TARGET = .

HEADERS = atomic.h clock.h histogram.h queue.h
SOURCES = act.c clock.c histogram.c queue.c
TARGET = act

CC = gcc
#CFLAGS = -g -O3 -fno-common -std=gnu99 -Wall -D_REENTRANT
CFLAGS = -g -fno-common -std=gnu99 -Wall -D_REENTRANT -D_FILE_OFFSET_BITS=64
CFLAGS += -MMD
LDFLAGS = $(CFLAGS)
INCLUDES = $(DIR_INCLUDE:%=-I%) -I/usr/include
LIBRARIES = -lpthread -lrt -lcrypto -lssl -lz

OBJECTS = $(SOURCES:%.c=$(DIR_OBJECT)/%.o)
DEPENDENCIES = $(OBJECTS:%.o=%.d)

default: $(TARGET)

.PHONY: clean
clean:
	/bin/rm -f $(OBJECTS) $(DEPENDENCIES) $(TARGET)

.PHONY: $(TARGET)
$(TARGET): $(OBJECTS)
	echo "Linking $@"
	$(CC) $(LDFLAGS) -o $(TARGET)  $(OBJECTS) $(LIBRARIES)

-include $(DEPENDENCIES)

$(DIR_OBJECT)/%.o:  %.c
	echo "Building $@"
	$(CC) $(CFLAGS)  -o $@ -c $(INCLUDES) $<

