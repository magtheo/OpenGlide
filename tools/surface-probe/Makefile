CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
X_LIBS  := $(shell pkg-config --libs x11 2>/dev/null)
X_FLAGS := $(shell pkg-config --cflags x11 2>/dev/null)

all: surface-probe

surface-probe: src/surface.c
	$(CC) $(CFLAGS) $(X_FLAGS) -o $@ src/surface.c $(X_LIBS)

clean:
	rm -f surface-probe

.PHONY: all clean
