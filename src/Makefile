# set environment variable RM_INCLUDE_DIR to the location of redismodule.h
ifndef RM_INCLUDE_DIR
        RM_INCLUDE_DIR=../
endif

ifndef RMUTIL_LIBDIR
        RMUTIL_LIBDIR=../rmutil/
endif

# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
        SHOBJ_CFLAGS ?=  -fno-common -g -ggdb
        SHOBJ_LDFLAGS ?= -shared
else
        SHOBJ_CFLAGS ?= -dynamic -fno-common -g -ggdb
        SHOBJ_LDFLAGS ?= -bundle -undefined dynamic_lookup
endif

CFLAGS = -I$(RM_INCLUDE_DIR) -g -fPIC -lc -lm -O0 -std=gnu99 
CC=gcc
.SUFFIXES: .c .so .o

MODULE = countminsketch

all: $(MODULE)

$(MODULE): %: %.o xxhash.o
	$(LD) -o $@.so $< xxhash.o $(SHOBJ_LDFLAGS) $(LIBS) -L$(RMUTIL_LIBDIR) -lrmutil -lc

clean:
	rm -rf *.so *.o
