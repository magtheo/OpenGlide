CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
X_FLAGS := $(shell pkg-config --cflags x11 2>/dev/null)
X_LIBS  := $(shell pkg-config --libs   x11 2>/dev/null)

all: swipe_capture

swipe_capture: src/swipe_capture.c
	$(CC) $(CFLAGS) $(X_FLAGS) -o $@ src/swipe_capture.c $(X_LIBS)

clean:
	rm -f swipe_capture

.PHONY: all clean
