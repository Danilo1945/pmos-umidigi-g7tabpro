CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -fPIC -Wall -std=gnu11 -fno-builtin -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
LDFLAGS ?= -shared -lpthread -ldl

TARGETS = libgpu_interface_driver.so libmali_shim.so

all: $(TARGETS)

libgpu_interface_driver.so: gpu_interface_driver.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

libmali_shim.so: libmali_shim.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS)

.PHONY: all clean
