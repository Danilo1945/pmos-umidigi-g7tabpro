CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -fPIC -Wall
LDFLAGS ?= -shared -lpthread -ldl

all: libmali_shim.so

libmali_shim.so: libmali_shim.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f libmali_shim.so

.PHONY: all clean

