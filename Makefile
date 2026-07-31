# ============================================================================
# WIREFIGHT 32X - top-level makefile
#
# Usage:
#   make            - build build/wirefight.32x (release ROM)
#   make testbuild  - build build/wirefight-test.32x (test status strip ROM)
#   make test       - build everything + PicoDrive harness and run the
#                     point-to-point test suite (see test/)
#
# Environment:
#   ROOTDIR - location of Chilly Willy's sega devkit
#             (default: /home/user/toolchain/opt/toolchains/sega)
# ============================================================================
ifndef ROOTDIR
ROOTDIR = /home/user/toolchain/opt/toolchains/sega
endif

PREFIX  = $(ROOTDIR)/sh-elf/bin/sh-elf-
CC      = $(PREFIX)gcc
AS      = $(PREFIX)as
OBJC    = $(PREFIX)objcopy

TITLE   = WIREFIGHT
VERSION = 0.1
MAPPER  = SEGA 32X
TARGET  = wirefight

ROMDIR = build

CCFLAGS = -c -std=c11 -m2 -mb -mtas -Os
CCFLAGS += -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers
CCFLAGS += -fomit-frame-pointer -fno-builtin -ffreestanding
CCFLAGS += -ffunction-sections -fdata-sections
CCFLAGS += -D__32X__ -DMARS
ifdef BENCH_NOPOSE
CCFLAGS += -DWF_BENCH_NOPOSE
endif
ifdef BENCH_NOSHADOW
CCFLAGS += -DWF_BENCH_NOSHADOW
endif
ifdef BENCH_NOBOXES
CCFLAGS += -DWF_BENCH_NOBOXES
endif
ifdef BENCH_NOCYL
CCFLAGS += -DWF_BENCH_NOCYL
endif
ifdef BENCH_NODRAW
CCFLAGS += -DWF_BENCH_NODRAW
endif
ifdef BENCH_NOSTAGE
CCFLAGS += -DWF_BENCH_NOSTAGE
endif
ifdef BENCH_NOFIGHT
CCFLAGS += -DWF_BENCH_NOFIGHT
endif
ifdef BENCH_NOUI
CCFLAGS += -DWF_BENCH_NOUI
endif
ifdef BENCH_NOSTRIP
CCFLAGS += -DWF_BENCH_NOSTRIP
endif
ifndef TESTBUILD
CCFLAGS += -DWF_RELEASE
TESTNAME =
BUILDDIR = build-obj
else
CCFLAGS += -DWF_TESTBUILD
TESTNAME = -test
BUILDDIR = build-test
endif

# BUILDDIR holds objects/map/elf (separate per variant so the test build
# never reuses release objects); final ROMs always land in $(ROMDIR).

LDFLAGS = -T $(ROOTDIR)/ldscripts/mars.ld -nostdlib \
          -Wl,--gc-sections,-Map=$(BUILDDIR)/$(TARGET)$(TESTNAME).map --specs=nosys.specs
LIBS    = -lgcc
ASFLAGS = --big

OBJS = \
	$(BUILDDIR)/crt0.o \
	$(BUILDDIR)/marsl.o \
	$(BUILDDIR)/gfx.o \
	$(BUILDDIR)/font.o \
	$(BUILDDIR)/sintab.o \
	$(BUILDDIR)/data.o \
	$(BUILDDIR)/sound.o \
	$(BUILDDIR)/wf.o \
	$(BUILDDIR)/main.o

all: $(ROMDIR)/$(TARGET)$(TESTNAME).32x

testbuild:
	$(MAKE) TESTBUILD=1 all

test:
	$(MAKE) -C test run

# ---- rom -------------------------------------------------------------------

$(ROMDIR)/$(TARGET)$(TESTNAME).32x: $(BUILDDIR)/$(TARGET)$(TESTNAME).elf $(ROMDIR)/romheaderfix
	$(OBJC) -O binary $< $(BUILDDIR)/wf_tmp.bin
	dd if=$(BUILDDIR)/wf_tmp.bin of=$@ bs=512K conv=sync status=none
	rm -f $(BUILDDIR)/wf_tmp.bin
	$(ROMDIR)/romheaderfix "$(MAPPER)" "$(TITLE) v$(VERSION)" $@
	@echo "ROM ready: $@"

$(BUILDDIR)/$(TARGET)$(TESTNAME).elf: $(OBJS) src-md/m68k.bin | $(BUILDDIR)
	$(CC) $(LDFLAGS) $(OBJS) $(LIBS) -o $@

$(ROMDIR)/romheaderfix: tools/romheaderfix.c | $(ROMDIR)
	gcc -O2 -o $@ tools/romheaderfix.c

$(ROMDIR):
	mkdir -p $(ROMDIR)

src-md/m68k.bin: src-md/main.c src-md/crt0.s
	$(MAKE) -C src-md ROOTDIR=$(ROOTDIR)

# ---- objects ----------------------------------------------------------------

$(BUILDDIR)/crt0.o: src-sh2/crt0.s src-md/m68k.bin | $(BUILDDIR)
	$(AS) $(ASFLAGS) -Isrc-sh2 src-sh2/crt0.s -o $@

$(BUILDDIR)/%.o: src-sh2/%.c src-sh2/data.h | $(BUILDDIR)
	$(CC) $(CCFLAGS) -Isrc-sh2 $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# ---- generated sources -------------------------------------------------------

GENSRC = src-sh2/data.h src-sh2/data.c src-sh2/sintab.c src-sh2/font.c src-sh2/font.h

$(GENSRC): ../wirefight-src/assets_vf/motions.npz tools/gen_data.py
	python3 tools/gen_data.py ../wirefight-src/assets_vf/motions.npz src-sh2/

generated: $(GENSRC)

clean:
	rm -f $(BUILDDIR)/*.o $(BUILDDIR)/*.bin $(BUILDDIR)/*.elf $(BUILDDIR)/*.map
	rm -f $(BUILDDIR)/$(TARGET)*.32x
	$(MAKE) -C src-md clean

.PHONY: all test testbuild clean generated
