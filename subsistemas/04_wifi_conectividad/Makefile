CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -static

all: wmt_init_complete

wmt_init_complete: wmt_init_complete.c
	$(CC) $(CFLAGS) -o wmt_init_complete wmt_init_complete.c

clean:
	rm -f wmt_init_complete
