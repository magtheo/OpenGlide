CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11 $(shell pkg-config --cflags ibus-1.0)
LIBS    := $(shell pkg-config --libs ibus-1.0)

all: ibus-probe

ibus-probe: main.c
	$(CC) $(CFLAGS) -o $@ main.c $(LIBS)

clean:
	rm -f ibus-probe

.PHONY: all clean
