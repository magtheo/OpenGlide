CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
PKG      = wayland-client
WL_CFLAGS = $(shell pkg-config --cflags $(PKG) 2>/dev/null)
WL_LIBS   = $(shell pkg-config --libs   $(PKG) 2>/dev/null)
X_CFLAGS  = $(shell pkg-config --cflags x11 xtst 2>/dev/null)
X_LIBS    = $(shell pkg-config --libs   x11 xtst 2>/dev/null)

GEN_DIR = generated
SRC_DIR = src

IM2_XML = protocols/input-method-unstable-v2.xml
IM2_HDR = $(GEN_DIR)/input-method-unstable-v2-client.h
IM2_SRC = $(GEN_DIR)/input-method-unstable-v2-client.c

OBJS = $(SRC_DIR)/main.o $(SRC_DIR)/xtest.o $(SRC_DIR)/uinput.o $(SRC_DIR)/im2.o $(IM2_SRC:.c=.o)

all: text-probe

text-probe: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(X_LIBS) $(WL_LIBS)

# src objects depend on the generated IM-v2 header; -Isrc finds probe.h.
$(SRC_DIR)/%.o: $(SRC_DIR)/%.c $(IM2_HDR) | $(GEN_DIR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(GEN_DIR) -I. $(X_CFLAGS) $(WL_CFLAGS) -c -o $@ $<

$(IM2_HDR): $(IM2_XML) | $(GEN_DIR)
	wayland-scanner client-header < $< > $@
$(IM2_SRC): $(IM2_XML) | $(GEN_DIR)
	wayland-scanner private-code  < $< > $@
$(GEN_DIR)/%.o: $(GEN_DIR)/%.c $(IM2_HDR)
	$(CC) $(CFLAGS) -I$(SRC_DIR) -I$(GEN_DIR) -I. $(WL_CFLAGS) -c -o $@ $<

$(GEN_DIR):
	mkdir -p $(GEN_DIR)

clean:
	rm -f text-probe $(SRC_DIR)/*.o $(GEN_DIR)/*.o $(GEN)

.PHONY: all clean
