CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -fPIC -Wall

TARGETS = libseat_direct.so backlight_lock touch_tool

all: $(TARGETS)

libseat_direct.so: libseat_direct.c
	$(CC) $(CFLAGS) -shared -o libseat_direct.so libseat_direct.c -nostdlib

backlight_lock: backlight_lock.c
	$(CC) -O2 -Wall -static -o backlight_lock backlight_lock.c

touch_tool: touch_tool.c
	$(CC) -O2 -Wall -static -o touch_tool touch_tool.c

clean:
	rm -f $(TARGETS)

.PHONY: all clean
