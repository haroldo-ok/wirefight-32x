# WIREFIGHT 32X - test suite build
CC      = gcc
CFLAGS  = -O2 -g -Wall -Wextra -Wno-unused-parameter

all: harness

harness: harness.c libretro.h
	$(CC) $(CFLAGS) -I. -o $@ harness.c -ldl

clean:
	rm -f harness

.PHONY: all clean
