CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -fPIC -Wall

all: libseat_direct.so

libseat_direct.so: libseat_direct.c
	$(CC) $(CFLAGS) -shared -o libseat_direct.so libseat_direct.c -nostdlib

clean:
	rm -f libseat_direct.so
