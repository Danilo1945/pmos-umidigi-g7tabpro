CROSS_COMPILE ?= aarch64-linux-gnu-
CC = $(CROSS_COMPILE)gcc
CFLAGS ?= -O2 -Wall -static

all: wdt_pet

wdt_pet: wdt_pet.c
	$(CC) $(CFLAGS) -o wdt_pet wdt_pet.c

clean:
	rm -f wdt_pet
