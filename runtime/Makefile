# Host (desktop / SDL2) build of the Luftrauser runtime.
# The PSP build uses CMakeLists.txt + cmake/psp-toolchain.cmake (needs pspdev).
#
#   make            -> build/luftrauser
#   make run        -> build + run against ../build/luftrauser/assets.pak
#   make spec       -> regenerate gen/spec_luftrauser.h from spec/luftrauser.toml

CC      ?= cc
CFLAGS  ?= -std=c11 -O2 -g -Wall -Wextra -Wno-unused-parameter
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

INC := -Isrc -I. $(SDL_CFLAGS)
SRC := \
  src/fp.c src/entity.c src/world.c src/pak.c src/ptx.c src/input.c \
  src/audio_mix.c src/drmp3_impl.c \
  game/game.c game/player.c game/uboot.c game/backdrop.c \
  game/fx.c game/enemy.c game/bullet.c game/brit.c game/jet.c game/sea.c game/text.c \
  platform/sdl/gfx_sdl.c platform/sdl/audio_sdl.c platform/sdl/main_sdl.c

OUT := build/luftrauser
PAK := ../build/luftrauser/assets.pak

all: spec $(OUT)

spec: gen/spec_luftrauser.h
gen/spec_luftrauser.h: ../spec/luftrauser.toml ../spec/gen_spec.py
	../.venv/bin/python ../spec/gen_spec.py

$(OUT): $(SRC) gen/spec_luftrauser.h
	@mkdir -p build
	$(CC) $(CFLAGS) $(INC) $(SRC) $(SDL_LIBS) -lm -o $@

run: all
	$(OUT) $(PAK)

clean:
	rm -rf build gen

.PHONY: all spec run clean
