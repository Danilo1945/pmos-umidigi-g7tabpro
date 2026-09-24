CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -static

all: battery_daemon

battery_daemon: battery_daemon.c battery_table.h
	$(CC) $(CFLAGS) -o battery_daemon battery_daemon.c

clean:
	rm -f battery_daemon
