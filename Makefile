# Builds Diary for AmigaOS 3.2 with bebbo's amiga-gcc.
#   make            build Diary and its icon into dist/
#   make install    copy dist/ to the Amiberry shared drive
TOOLCHAIN ?= $(HOME)/Projects/amiga-toolchain
CC := $(TOOLCHAIN)/bin/m68k-amigaos-gcc
CFLAGS := -Os -m68020 -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign -fomit-frame-pointer
LDFLAGS := -s -noixemul
INSTALL_DIR ?= $(HOME)/Amiberry/HardDrives/Shared/Diary

all: dist/Diary dist/Diary.info

dist/Diary: diary.c | dist
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

dist/Diary.info: tools/make_icon.py | dist
	python3 tools/make_icon.py $@

dist:
	mkdir -p dist

install: all
	mkdir -p "$(INSTALL_DIR)"
	cp dist/Diary dist/Diary.info "$(INSTALL_DIR)/"

clean:
	rm -rf dist

.PHONY: all install clean
