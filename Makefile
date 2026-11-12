# Quake Anthology (C port of quake-anthology-ts)
# GNU Make only; no CMake/Meson required. GPL-2.0-or-later.
CC ?= cc
AR ?= ar

CPPFLAGS += -Iinclude $(shell pkg-config --cflags sdl2 freetype2 vorbisfile theoradec)
# -ffp-contract=off preserves the staged binary32 rounding of the TypeScript
# Math.fround call sites bit for bit; do not add -ffast-math.
CFLAGS += -std=c17 -Wall -Wextra -Wpedantic -O2 -g -ffp-contract=off
LDLIBS += -lm -lz -lcurl -lcrypto $(shell pkg-config --libs sdl2 freetype2 vorbisfile theoradec gl)

BUILD := build

# Every ported source joins the archive automatically; compile breakage in a
# freshly ported module fails the build immediately.
LIB_SRCS := $(shell find src -name '*.c' ! -name 'main.c' | sort)
LIB_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(LIB_SRCS))
APP_SRCS := src/main.c
APP_OBJS := $(patsubst %.c,$(BUILD)/%.o,$(APP_SRCS))

BIN := $(BUILD)/quake-anthology
LIB := $(BUILD)/libquake-anthology.a

.PHONY: all clean
all: $(BIN)

$(BIN): $(APP_OBJS) $(LIB)
	$(CC) $(CFLAGS) $(APP_OBJS) $(LIB) -o $@ $(LDLIBS)

# `ar r` replaces members by basename, so objects that share a basename in
# different directories would drop each other; rebuild with `q` instead.
$(LIB): $(LIB_OBJS)
	rm -f $@
	$(AR) qc $@ $(LIB_OBJS)
	ranlib $@

$(BUILD)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

clean:
	rm -rf $(BUILD)

-include $(LIB_OBJS:.o=.d) $(APP_OBJS:.o=.d)
