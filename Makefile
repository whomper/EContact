# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EContact - contacts for the Atari ST/STE/TT/Falcon, in C, a desk
# accessory like ECal and a companion to EMail.
#
#   make            builds binary/econtact.acc (the desk accessory) and econtact.prg
#                   (the same program, to start from the desktop), for every Atari,
#                   with a stock m68k ELF cross compiler (Debian/Ubuntu: apt install
#                   gcc-m68k-linux-gnu), and copies TLS.OVL beside them.
#                   No MiNTLib: the program is freestanding and common/tools/elf2tos.py
#                   writes the TOS header.
#   make test       builds the contacts core for this computer and runs the
#                   unit tests; `make itest` also syncs with a local CardDAV
#                   server (Radicale).
#   make help       binary/econtact.hyp, the guide for ST-Guide (needs hcp:
#                   common/tools/build-hcp.sh builds it)
#   make dist       a release's files in dist/: each file, and econtact-x.y.zip
#
# binary/ is what make builds, never committed: releases carry the
# programs (.github/workflows/release.yml).
#
# The Atari runtime, TLS, HTTP, WebDAV and Hebrew text come from ECal and
# EMail (github.com/whomper/ECal, github.com/whomper/EMail).

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging -fdisable-tree-bswap \
           -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -DCAL_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Icommon/atari/include \
           -Isrc -Icommon/src -Iatari -Icommon/atari -Iui -Icommon/ui -Icommon/third_party/bearssl/inc \
           -Ithird_party/tjpgd
# each function and datum in a section of its own, and the link drops what
# nothing uses (--gc-sections): common's code this program doesn't call
# stays out of the accessory's memory (the hub, 1 Nov 2026)
CFLAGS  += -ffunction-sections -fdata-sections
# common's png.c made small (the same pictures, every format: the hub,
# c36aba7): 2.8 KB more for a program beside the accessory on a 1 MB ST
CFLAGS  += -DPNG_SLIM -DPNG_STEPS
# `make EXTRA_CFLAGS=-DAES_PROFILE`: stretches between event calls over 50 ms
# to the emulator's console (common/atari/gem.c)
CFLAGS  += $(EXTRA_CFLAGS)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,common/atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments -Wl,--gc-sections

# the AES calls that may let another program run: the accessory lets go
# of its files first (ui/evwrap.c, the guidelines 2.2)
EVWRAP  := -Wl,--wrap=evnt_multi_,--wrap=evnt_timer_,--wrap=form_do,--wrap=form_alert,--wrap=fsel_exinput

# BearSSL 0.6: on the Atari only MD5 and its byte order (Digest logins),
# as 68000 code (no byte reads joined into word reads: the guidelines,
# The machines); the TLS is TLS.OVL's (below). The host builds take all
# of it (HBSSLOBJ)
BSSL    := common/third_party/bearssl
BSSLSRC := $(wildcard $(BSSL)/src/*/*.c)
BSSLDEF := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 \
           -DBR_USE_WIN32_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0
BSSLST  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -w \
           -nostdinc -isystem $(GCCINC) -Iatari/include -Icommon/atari/include -I$(BSSL)/inc -I$(BSSL)/src $(BSSLDEF) \
           -fno-store-merging -fdisable-tree-bswap
STBSSL  := hash/md5 codec/dec32le codec/enc32le
HBSSLOBJ = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/hbssl/%.o,$(BSSLSRC))

# the TLS (tls.c, BearSSL, the DSP's RSA and curves, 68030 code) is in
# TLS.OVL, the family's one file (common/tlsovl, the guidelines 7.4),
# loaded on a 68020 or better the first time a Direct connection needs
# it (tls_ovl.c, ovl.c, dsp_ovl.c); an ST never loads it. Copied, never
# built here
TLSOVL  := common/tlsovl/tls.ovl

CORE    := util charset conn ovl bidi secret futil ctime xml http vcard carddav abook photo photopack jpegdc pfkeep
# (linked last, after cpucheck: 2.15's PNG photos, so the code before them stays
# where 2.14 had it; the host builds take them with the core)
CORE_END := inflate png
ATARI   := libc tos plat_tos net_tos sting gem tls_ovl pic_ovl bigbuf
UI      := main evwrap win draw popup panebar dsp_ovl sidebar list card editor dialogs field about font fontsel accmem help av av_people alert dialog aboutbox iconify clip dd mbar twin busy bgwork people_ask answer notify aboutline text offdraw click hebkey pic arrowed tooltip progbar dlgbuild

OBJDIR  := build
OBJS    := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/tjpgd.o $(OBJDIR)/crash.o $(OBJDIR)/crash_s.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI))) \
           $(addprefix $(OBJDIR)/bssl/,$(addsuffix .o,$(notdir $(STBSSL)))) $(OBJDIR)/version.o \
           $(OBJDIR)/cpualert.o $(OBJDIR)/cpucheck.o $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE_END)))
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h \
           common/src/*.h common/atari/*.h common/atari/include/*.h common/ui/*.h common/dsp/*.h \
           third_party/tjpgd/*.h)

# what goes onto the Atari: binary/ (built, never committed)
BIN     := binary

# the help file for ST-Guide, from docs/GUIDE.md, needs HCP (hcp, from
# github.com/th-otto/hypview; common/tools/build-hcp.sh builds it): made
# with the program when hcp is there, else by `make help HCP=/path/to/hcp`.
# `make test` and `make dist` check it says the program's version
HCP     ?= hcp
HAVEHCP := $(shell command -v $(HCP) 2>/dev/null)

all: $(BIN)/econtact.acc $(BIN)/econtact.prg $(BIN)/tls.ovl $(BIN)/tlsfb.ovl $(BIN)/pic.ovl $(BIN)/roots.dat $(if $(HAVEHCP),$(BIN)/econtact.hyp)

help: $(BIN)/econtact.hyp

# ECONTACT.REF (the pages' names, for ST-Guide's index) is written with it
$(BIN)/econtact.hyp: docs/GUIDE.md tools/mkhelp.py VERSION docs/front.json docs/help/WINDOW.IMG \
                     common/help/ECONTACT.IMG common/tools/helpfront.py common/tools/gemimg.py | $(OBJDIR) $(BIN)
	python3 tools/mkhelp.py docs/GUIDE.md VERSION $(OBJDIR)/ECONTACT.STG
	python3 common/tools/helpfront.py page docs/front.json $(OBJDIR)/ECONTACT.STG $(OBJDIR)/ECONTACT.STG
	$(HCP) -q -o $@ $(OBJDIR)/ECONTACT.STG >/dev/null
	test -s $(BIN)/econtact.ref

# one program, two names: it sees at start-up whether TOS loaded it as
# an accessory or started it as an application
$(BIN)/econtact.acc: $(OBJDIR)/contact.elf common/tools/elf2tos.py | $(BIN)
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@
$(BIN)/econtact.prg: $(BIN)/econtact.acc
	cp $< $@

# Placement (the guidelines' Speed; common/atari/codepad.S, as EWeb keeps its
# places): room after the objects that grew since 2.11, so every function and
# buffer of the others is where 2.11 had it modulo 256, as a Falcon's caches
# see it. 2.12's shared code (pic.c's soft rows, libc, ovl, accmem, av) and
# photos (draw.c) moved them 2.4 KB: a row pressed on Falcon-4:16 50 ms
# against 40, a letter typed in the search 45 against 35 (six pairs each);
# with the room, 45 and 35. 2.13's cpucheck.c and cpualert.c, last, moved the
# read-only data, the data and the BSS. 2.15: placed again against 2.14 (its
# PNG code, inflate and png, linked last; http.c, photo.c and common's text.c
# grown). name:code[:bss[:ro[:data[:strings]]]] bytes, worked out with
# common/tools/profpad.py --ship (the last release's ELF) over this link's objects:
# change them when one of these objects' code or BSS changes, measured
# with speed.py

, := ,
LPAD    := ovl:254 http:104:100:196 photo:54:246 libc:180:0:252 pic_ovl:158 main:190 draw:106:254 accmem:208 \
           av:242 text:226:252 pic:28 png:122:136:16:232:166
ifeq ($(PADS),no)
LPAD    :=
endif
lpad_o   = $(OBJDIR)/lpad_$(word 1,$(subst :, ,$(1))).o
LINKOBJS := $(foreach o,$(OBJS),$(o) $(foreach p,$(LPAD),$(if $(filter $(basename $(notdir $(o))),$(word 1,$(subst :, ,$(p)))),$(call lpad_o,$(p)))))
lpad_n   = $(word $(2),$(subst :, ,$(1)))
LPADU   := $(foreach p,$(LPAD),-Wl,-u,lpad_$(call lpad_n,$(p),1) \
             $(if $(filter-out 0,$(call lpad_n,$(p),3)),-Wl$(,)-u$(,)lpad_$(call lpad_n,$(p),1)_bss) \
             $(if $(filter-out 0,$(call lpad_n,$(p),4)),-Wl$(,)-u$(,)lpad_$(call lpad_n,$(p),1)_ro) \
             $(if $(filter-out 0,$(call lpad_n,$(p),5)),-Wl$(,)-u$(,)lpad_$(call lpad_n,$(p),1)_data) \
             $(if $(filter-out 0,$(call lpad_n,$(p),6)),-Wl$(,)-u$(,)lpad_$(call lpad_n,$(p),1)_str))
define lpad_rule
$(call lpad_o,$(1)): common/atari/codepad.S Makefile | $(OBJDIR)
	$(CC) -m68000 -DPAD_NAME=lpad_$(word 1,$(subst :, ,$(1))) -DPAD_TEXT=$(word 2,$(subst :, ,$(1))) \
	    -DPAD_RO=$(or $(word 4,$(subst :, ,$(1))),0) -DPAD_DATA=$(or $(word 5,$(subst :, ,$(1))),0) \
	    -DPAD_BSS=$(or $(word 3,$(subst :, ,$(1))),0) -DPAD_STR=$(or $(word 6,$(subst :, ,$(1))),0) -c $$< -o $$@
endef
$(foreach p,$(LPAD),$(eval $(call lpad_rule,$(p))))

$(OBJDIR)/contact.elf: $(LINKOBJS) common/atari/link.ld
	$(CC) $(LDFLAGS) $(EVWRAP) $(LPADU) -Wl,-Map,$@.map -o $@ $(LINKOBJS) -lgcc

# `make prof`: the speed rig's build (AES_PROFILE=20 in the files that
# time; the rest the shipped objects) at the shipped build's places
# modulo 256 (common/tools/profpad.py; AERA-Atari 098183d, f89e601), in
# $(OBJDIR)/prof/bin for speed.py --bin and quiet_start.py
PROFDIR   := $(OBJDIR)/prof
PROF_TIMED := main win card field abook photo gem
PROF_OBJS := $(foreach o,$(LINKOBJS),$(if $(filter $(basename $(notdir $(o))),$(PROF_TIMED)),$(PROFDIR)/obj/$(notdir $(o)),$(o)))
prof: $(PROFDIR)/bin/econtact.acc
$(PROFDIR)/bin/econtact.acc: $(PROFDIR)/contact.elf common/tools/elf2tos.py
	@mkdir -p $(dir $@)
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@
	cp $@ $(PROFDIR)/bin/econtact.prg
$(PROFDIR)/contact.elf: $(OBJDIR)/contact.elf $(addprefix $(PROFDIR)/obj/,$(addsuffix .o,$(PROF_TIMED))) \
                        common/tools/profpad.py common/atari/codepad.S
	python3 common/tools/profpad.py --ship $(OBJDIR)/contact.elf --out $@ --cpu=-m68000 --dir $(PROFDIR)/pads \
	    $(foreach t,$(PROF_TIMED),--exclude $(OBJDIR)/$(t).o --exclude $(PROFDIR)/obj/$(t).o) \
	    -- $(CC) $(LDFLAGS) $(EVWRAP) $(LPADU) $(PROF_OBJS) -lgcc
$(addprefix $(PROFDIR)/obj/,$(addsuffix .o,$(PROF_TIMED))): FORCE
	$(MAKE) --no-print-directory OBJDIR=$(PROFDIR)/obj EXTRA_CFLAGS=-DAES_PROFILE=20 $@

$(BIN)/tls.ovl: $(TLSOVL) | $(BIN)
	cp $< $@

# TLSFB.OVL: TLS.OVL's ColdFire build, loaded on a FireBee (common/tlsovl)
$(BIN)/tlsfb.ovl: common/tlsovl/tlsfb.ovl | $(BIN)
	cp $< $@

# PIC.OVL: a photo's dither on a Falcon's DSP (common/picovl), copied as TLS.OVL
PICOVL  := common/picovl/pic.ovl
$(BIN)/pic.ovl: $(PICOVL) | $(BIN)
	cp $< $@

$(OBJDIR)/bssl/%.o: $(BSSL)/src/hash/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BSSLST) -c -o $@ $<
$(OBJDIR)/bssl/%.o: $(BSSL)/src/codec/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BSSLST) -c -o $@ $<

$(OBJDIR)/%.o: src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/src/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/atari/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
$(OBJDIR)/%.o: common/ui/%.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c -o $@ $<
# common's dialog.c serves the Font dialog alone here: without the parts EContact
# never calls (dlg_later's slices, the kept press, dlg_keys, dlg_badge: DLG_NO_LATER,
# AERA-Atari 8f542df), 556 bytes a 1 MB ST's contacts need
$(OBJDIR)/dialog.o: CFLAGS += -DDLG_NO_LATER
# common's pictures (pic.c): its palette and dither draw the photos (ui/draw.c;
# Erez, 8 Nov 2026), no PNG or GIF decoder (the link keeps only what is called)
# nor its rows for pictures without alpha (PIC_NO_FAST_ROWS, AERA-Atari 18edfb7): every row
# EContact dithers has the ellipse as its alpha, and their 1.9 KB were a 1 MB ST's contacts
$(OBJDIR)/pic.o: common/src/pic.c $(HDRS) | $(OBJDIR)
	$(CC) $(CFLAGS) -DPIC_NO_PNG -DPIC_NO_GIF -DPIC_NO_FAST_ROWS -Icommon/third_party/nsgif/include -c -o $@ $<
# ChaN's JPEG decoder for the photos: 68000 code, for speed. Its IDCT
# overflows on a damaged photo's coefficients: -fwrapv (here and in every
# host build) makes that the wrap the 68000 does anyway, not undefined
# behaviour; its pixels are clipped through a masked table
TJPGD   := third_party/tjpgd/tjpgd.c third_party/tjpgd/tjpgd.h third_party/tjpgd/tjpgdcnf.h
$(OBJDIR)/tjpgd.o: $(TJPGD) | $(OBJDIR)
	$(CC) $(subst -Os,-O2,$(CFLAGS)) -fwrapv -w -c -o $@ $<

# the stack: 18.25 KB (18,688 bytes), not crt0's 48 (2.14: 24; 2.15 took 5 KB for PNG photos; the
# PNG path's frames, -fstack-usage, add some 1.6 KB to the 13.2 measured). The accessory's load is
# what a program started beside it on a 1 MB ST doesn't have: 2.13's
# 1.5 KB more left EFTP (359,200 bytes) without room to start; 24 KB
# gave a program 8 KB more than 32, 20 another 4 (tests/hatari/acc_room.py). The
# deepest measured, from the BSS's zeros, was 13.2 KB (Sync Contact
# Photos on ST-1: acc_files.py), 8.7 the accessory's refresh
# (quiet_sync.py), 6.2 the dialogs, the editor and the speed rig's
# actions, 1.7 the Falcon's TLS (falcon_tls.py)
$(OBJDIR)/crt0.o: common/atari/crt0.S Makefile | $(OBJDIR)
	$(CC) -m68000 -DSTACK_SIZE=18688 -c -o $@ $<
$(OBJDIR)/sting_s.o: common/atari/sting.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/nf.o: common/atari/nf.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/crash_s.o: common/atari/crash.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<

$(OBJDIR) $(BIN):
	mkdir -p $@

# EContact's icon (the guidelines 5.5): drawn in common/icons/ECONTACT.txt
# (AERA-Atari), made by common/tools/appicon.py into icons/ (a release
# carries them), ui/icon.h and icon16.h, and docs/icon*.png; committed
icons:
	python3 common/tools/appicon.py ECONTACT --rsc icons/ECONTACT.RSC --rscnv icons/ECONTNV.RSC \
		--icn icons/ECONTACT.ICN --mask icons/ECONTMSK.ICN --headers ui --docs docs

# the version: VERSION holds the last release (major.minor, e.g. 1.33),
# and the build never writes it; every build shows it as it is.
# tools/bump.py starts a release.
VERSION_STR := $(shell cat VERSION)

# the parser's own number: BOOKnnnn.IDX keeps what vcard_parse made, and
# is good for every build whose parser is the same source (a new version
# alone doesn't read all the cards again: half a minute on an ST)
PARSE_SRC := src/vcard.c src/vcard.h common/src/charset.c common/src/charset.h common/src/charset_tab.h \
	     common/src/bidi.c common/src/bidi.h common/src/util.c common/src/util.h
PARSE_ID  := $(shell cat $(PARSE_SRC) | cksum | cut -d' ' -f1)

# written again whenever the string changes
$(OBJDIR)/version.c: FORCE | $(OBJDIR)
	@printf '/* written by the Makefile: do not edit */\nconst char app_version[] = "%s";\nconst char parse_id[] = "P%s";\n' "$(VERSION_STR)" "$(PARSE_ID)" > $@.new
	@if cmp -s $@.new $@; then rm $@.new; else mv $@.new $@; fi
FORCE:
$(OBJDIR)/version.o: $(OBJDIR)/version.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- host build: the same contacts core, for tests and card-cli ----
HOSTCC     ?= cc
HOSTCFLAGS := -DPNG_STEPS -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
              -Wno-format-truncation -Isrc -Icommon/src -Ithird_party/tjpgd -fsanitize=address,undefined
# the build machine's builds link tls.c, which answers tls_ovl_* too (7.4)
HOSTSRC    := $(foreach m,$(patsubst ovl,tls,$(CORE) $(CORE_END)),$(firstword $(wildcard src/$(m).c common/src/$(m).c))) tests/host/plat_posix.c tests/host/version_host.c
HOSTLIB    := $(OBJDIR)/libbearssl-host.a $(OBJDIR)/host-tjpgd.o

$(OBJDIR)/host-tjpgd.o: $(TJPGD) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -fwrapv -w -c -o $@ $<
$(OBJDIR)/plain-tjpgd.o: $(TJPGD) | $(OBJDIR)
	$(HOSTCC) $(filter-out -fsanitize=%,$(HOSTCFLAGS)) -fwrapv -w -c -o $@ $<
$(OBJDIR)/fuzz-tjpgd.o: $(TJPGD) | $(OBJDIR)
	$(FUZZCC) $(subst -fsanitize=fuzzer,-fsanitize=fuzzer-no-link,$(FUZZFLAGS)) -fwrapv -w -c -o $@ $<

$(OBJDIR)/hbssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -I$(BSSL)/inc -I$(BSSL)/src -c -o $@ $<

$(OBJDIR)/libbearssl-host.a: $(HBSSLOBJ)
	rm -f $@
	ar rcs $@ $^

# PEOPLE's answers (ui/answer.c, common/ui/people_ask.c) built in too,
# with common/tests/people's stand-ins for the AES (card-cli ask)
ASKSRC  := ui/answer.c common/ui/people_ask.c
$(OBJDIR)/card-cli: tests/host/card_cli.c $(HOSTSRC) $(ASKSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -Icommon/tests/people/stub -Icommon/ui -Iui -I$(BSSL)/inc -o $@ tests/host/card_cli.c \
	  $(HOSTSRC) $(ASKSRC) $(HOSTLIB)

$(OBJDIR)/unit: tests/unit.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/unit.c $(HOSTSRC) $(HOSTLIB)

# without the sanitizers: it refuses big mallocs itself (--wrap)
$(OBJDIR)/lowmem: tests/lowmem.c $(HOSTSRC) $(HDRS) $(OBJDIR)/libbearssl-host.a $(OBJDIR)/plain-tjpgd.o | $(OBJDIR)
	$(HOSTCC) $(filter-out -fsanitize=%,$(HOSTCFLAGS)) -I$(BSSL)/inc -Wl,--wrap=malloc,--wrap=realloc,--wrap=calloc \
		-o $@ tests/lowmem.c $(HOSTSRC) $(OBJDIR)/libbearssl-host.a $(OBJDIR)/plain-tjpgd.o

$(OBJDIR)/mkroots: common/tools/mkroots.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ common/tools/mkroots.c $(HOSTSRC) $(HOSTLIB)

# the root certificates: Mozilla's list, kept in common/
$(BIN)/cacert.pem: common/certs/CACERT.PEM | $(BIN)
	cp $< $@
$(BIN)/roots.dat: $(BIN)/cacert.pem
	$(MAKE) $(OBJDIR)/mkroots
	$(OBJDIR)/mkroots $(BIN)/cacert.pem $@

# a stand-in for ST-Guide, for tests/hatari/help_key.py
$(OBJDIR)/fakeguide.acc: tests/atari/fakeguide.c $(OBJDIR)/crt0.o $(OBJDIR)/gem.o $(OBJDIR)/libc.o $(OBJDIR)/tos.o $(OBJDIR)/nf.o
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/fakeguide.o $<
	$(CC) $(LDFLAGS) -o $(OBJDIR)/fakeguide.elf $(OBJDIR)/crt0.o $(OBJDIR)/fakeguide.o \
		$(OBJDIR)/gem.o $(OBJDIR)/libc.o $(OBJDIR)/tos.o $(OBJDIR)/nf.o -lgcc
	python3 common/tools/elf2tos.py $(OBJDIR)/fakeguide.elf $@

# reading cards timed on the Atari, for tests/hatari/vcbench.py
VCBOBJ := $(addprefix $(OBJDIR)/,crt0.o vcard.o charset.o util.o libc.o tos.o nf.o)
$(OBJDIR)/VCBENCH.PRG: tests/atari/vcbench.c $(VCBOBJ)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/vcbench.o $<
	$(CC) $(LDFLAGS) -o $(OBJDIR)/vcbench.elf $(OBJDIR)/crt0.o $(OBJDIR)/vcbench.o $(filter-out %crt0.o,$(VCBOBJ)) -lgcc
	python3 common/tools/elf2tos.py $(OBJDIR)/vcbench.elf $@

# the room TOS gives a program after ECONTACT.ACC's boot, for tests/hatari/acc_room.py
$(OBJDIR)/memprobe.prg: tests/atari/memprobe.c $(OBJDIR)/crt0.o $(OBJDIR)/libc.o $(OBJDIR)/tos.o $(OBJDIR)/nf.o
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/memprobe.o $<
	$(CC) $(LDFLAGS) -o $(OBJDIR)/memprobe.elf $(OBJDIR)/crt0.o $(OBJDIR)/memprobe.o \
		$(OBJDIR)/libc.o $(OBJDIR)/tos.o $(OBJDIR)/nf.o -lgcc
	python3 common/tools/elf2tos.py $(OBJDIR)/memprobe.elf $@

# a stand-in for another program's accessory (its alert), for tests/hatari/xaaes_dialogs.py
$(OBJDIR)/alertacc.acc: tests/atari/alertacc.c $(OBJDIR)/crt0.o $(OBJDIR)/gem.o $(OBJDIR)/libc.o $(OBJDIR)/tos.o $(OBJDIR)/nf.o
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/alertacc.o $<
	$(CC) $(LDFLAGS) -o $(OBJDIR)/alertacc.elf $(OBJDIR)/crt0.o $(OBJDIR)/alertacc.o \
		$(OBJDIR)/gem.o $(OBJDIR)/libc.o $(OBJDIR)/tos.o $(OBJDIR)/nf.o -lgcc
	python3 common/tools/elf2tos.py $(OBJDIR)/alertacc.elf $@

# a stand-in for EMail asking about people, for tests/hatari/people_ask.py
ASKOBJ := $(addprefix $(OBJDIR)/,crt0.o gem.o libc.o tos.o nf.o av.o av_people.o people_ask.o)
$(OBJDIR)/ASKER.PRG: tests/atari/asker.c $(ASKOBJ)
	$(CC) $(CFLAGS) -c -o $(OBJDIR)/asker.o $<
	$(CC) $(LDFLAGS) -o $(OBJDIR)/asker.elf $(OBJDIR)/crt0.o $(OBJDIR)/asker.o $(filter-out %crt0.o,$(ASKOBJ)) -lgcc
	python3 common/tools/elf2tos.py $(OBJDIR)/asker.elf $@

# ---- fuzzing (the AERA guidelines, 7.1): what servers send, from
# libFuzzer, with AddressSanitizer and UBSan. The network is
# common/tests/fuzz/fuzz_net.c: the server says the fuzz input. Needs
# clang with libFuzzer (Debian/Ubuntu: clang, libclang-rt-dev); make fuzz
# runs each target FUZZTIME seconds from tests/fuzz/seeds (mkseeds.py)
FUZZCC    ?= clang
FUZZTIME  ?= 60
FUZZERS   := dav vcard jpeg xml inflate
FUZZFLAGS := -g -O1 -fsanitize=fuzzer,address,undefined -fno-sanitize-recover=undefined -DFUZZ_NET \
             -Isrc -Icommon/src -Ithird_party/tjpgd -Icommon/tests/fuzz -I$(BSSL)/inc

$(OBJDIR)/fuzz_%: tests/fuzz/fuzz_%.c $(HOSTSRC) $(HDRS) $(OBJDIR)/libbearssl-host.a $(OBJDIR)/fuzz-tjpgd.o \
		common/tests/fuzz/fuzz_net.c | $(OBJDIR)
	$(FUZZCC) $(FUZZFLAGS) -o $@ $< $(HOSTSRC) common/tests/fuzz/fuzz_net.c $(OBJDIR)/libbearssl-host.a \
		$(OBJDIR)/fuzz-tjpgd.o

fuzz: $(addprefix $(OBJDIR)/fuzz_,$(FUZZERS))
	@for f in $(FUZZERS); do \
	  mkdir -p $(OBJDIR)/corpus/$$f; \
	  echo "fuzz_$$f: $(FUZZTIME) s"; \
	  $(OBJDIR)/fuzz_$$f -max_total_time=$(FUZZTIME) -timeout=10 -rss_limit_mb=1024 \
	    -artifact_prefix=$(OBJDIR)/crash-$$f- $(OBJDIR)/corpus/$$f tests/fuzz/seeds/$$f \
	    $$(ls -d tests/fuzz/found/$$f 2>/dev/null) \
	    > $(OBJDIR)/fuzz_$$f.log 2>&1 || { tail -40 $(OBJDIR)/fuzz_$$f.log; exit 1; }; \
	  grep -E "^#[0-9]+.*DONE" $(OBJDIR)/fuzz_$$f.log | tail -1; \
	done

# the same targets without libFuzzer (common/tests/fuzz/replay.c), with
# this computer's compiler: make test runs the seeds again, and the inputs
# that once broke a parser (tests/fuzz/found)
REPLAYFLAGS := $(HOSTCFLAGS) -fno-sanitize-recover=undefined -DFUZZ_NET -Icommon/tests/fuzz -I$(BSSL)/inc -Ithird_party/tjpgd
REPLAYSRC   := common/tests/fuzz/fuzz_net.c common/tests/fuzz/replay.c

$(OBJDIR)/replay_%: tests/fuzz/fuzz_%.c $(HOSTSRC) $(HDRS) $(HOSTLIB) $(REPLAYSRC) | $(OBJDIR)
	$(HOSTCC) $(REPLAYFLAGS) -o $@ $< $(HOSTSRC) $(REPLAYSRC) $(HOSTLIB)

test: $(OBJDIR)/unit $(OBJDIR)/lowmem $(addprefix $(OBJDIR)/replay_,$(FUZZERS)) $(OBJDIR)/contact.elf
	$(OBJDIR)/unit
	$(OBJDIR)/lowmem
	@# no word or long at an odd stack offset: an address error on a 68000
	@# (the guidelines, The machines; common's accmem, 27 Oct 2026)
	python3 common/tools/oddaccess.py $(OBJDIR)/contact.elf \
	  $(wildcard $(OBJDIR)/fakeguide.elf $(OBJDIR)/alertacc.elf $(OBJDIR)/asker.elf $(OBJDIR)/vcbench.elf)
	@for f in $(FUZZERS); do \
	  $(OBJDIR)/replay_$$f tests/fuzz/seeds/$$f $$(ls -d tests/fuzz/found/$$f 2>/dev/null) || exit 1; \
	done
	@$(HYPCHECK)

# the help file says the program's version (when it was made)
HYPCHECK = test ! -f $(BIN)/econtact.hyp || grep -q "ECONTACT.HYP $$(cat VERSION)" $(BIN)/econtact.hyp || \
	  { echo "$(BIN)/econtact.hyp isn't version $$(cat VERSION): make help"; exit 1; }

# the FireBee package (the guidelines, 7.4; Erez, 22 Nov 2026: two
# packages, the same names inside): the same sources as ColdFire code, by
# common's mkfirebee.py from the rules above (cpucheck.c becomes
# cpucheck_cf.S, -lgcc ll64.c, no LPAD: the 68030's caches aren't the
# ColdFire's), one ELF for ECONTACT.PRG and .ACC as here, in $(BIN)/firebee/
FBBIN   := $(BIN)/firebee
firebee: $(FBBIN)/econtact.prg $(FBBIN)/econtact.acc
$(FBBIN)/econtact.acc: $(OBJDIR)/contact.elf common/tools/mkfirebee.py
	@mkdir -p $(FBBIN)
	python3 common/tools/mkfirebee.py --elf $(OBJDIR)/contact.elf --make "PADS=no" --outdir $(OBJDIR)/cf/ $@
$(FBBIN)/econtact.prg: $(FBBIN)/econtact.acc
	cp $< $@

# a release: two packages and nothing else (the guidelines, 7.4; Erez,
# 22 Nov 2026: "two ZIP files, one for each platform"),
# dist/econtact-x.y.zip for the ST, TT and Falcon and
# dist/econtact-x.y-firebee.zip for the FireBee, the same names in both
# (the icons in icons/): the FireBee's ECONTACT.PRG and .ACC (make
# firebee) and TLS.OVL, common's ColdFire build (tlsfb.ovl) under the
# standard name; no PIC.OVL there (68030 and DSP code: pic_ovl.c never
# loads it on a FireBee). Every name in lowercase (the guidelines 6.6)
DIST    := $(BIN)/econtact.acc $(BIN)/econtact.prg $(BIN)/tls.ovl $(BIN)/pic.ovl $(BIN)/cacert.pem $(BIN)/roots.dat \
           $(BIN)/econtact.hyp $(BIN)/econtact.ref
FBDIST  := $(FBBIN)/econtact.acc $(FBBIN)/econtact.prg $(BIN)/cacert.pem $(BIN)/roots.dat \
           $(BIN)/econtact.hyp $(BIN)/econtact.ref
ICONS   := icons/ECONTACT.RSC icons/ECONTNV.RSC icons/ECONTACT.DEF icons/ECONTACT.ICN icons/ECONTMSK.ICN
dist: all firebee help
	@$(HYPCHECK)
	rm -rf dist
	mkdir -p dist/zip/icons dist/fbzip/icons
	cp $(DIST) dist/zip/
	cp $(FBDIST) dist/fbzip/
	cp $(BIN)/tlsfb.ovl dist/fbzip/tls.ovl
	for f in $(ICONS); do n=$$(basename $$f | tr A-Z a-z); \
	  cp $$f dist/zip/icons/$$n && cp $$f dist/fbzip/icons/$$n || exit 1; done
	cd dist/zip && zip -q -9 -r ../econtact-$(VERSION_STR).zip .
	cd dist/fbzip && zip -q -9 -r ../econtact-$(VERSION_STR)-firebee.zip .
	rm -rf dist/zip dist/fbzip
	@if ls dist | grep -q '[A-Z]' || unzip -Z1 dist/econtact-$(VERSION_STR).zip | grep -q '[A-Z]' || \
	    unzip -Z1 dist/econtact-$(VERSION_STR)-firebee.zip | grep -q '[A-Z]'; then \
		echo "dist: a name with capitals (the guidelines, 6.6)"; exit 1; fi

# ---- the core as ColdFire code (the FireBee package, the guidelines 7.4) ----
# tests/corecheck.c (vcard.c as tests/host/vcdump.c says it, the search,
# the photos made into EContact's picture with TJpgDec's two 68000 changes
# and jpegdc.c) against common's atari/libc.c, built twice as the program
# is: for the FireBee (-mcpu=5475, run under qemu-m68k -cpu cfv4e, no
# libgcc: common's ll64.c, as mkfirebee.py) and for the ST (-m68000, under
# qemu-m68k -cpu m68000; its 32-bit * and / common's tos.c's, as the
# program's, not the build machine's libgcc, which is 68020 code);
# tests/coldfire/cfrun.c stands in for GEMDOS's
# memory and the files. The FireBee's output must be the ST's, byte for
# byte (not this computer's: its long is 64 bits, and vcard_photo_key's
# rotation gives another key here), over the sample cards, the fuzz seeds
# and tests/host/vcgen.py's cards (fixed seeds)
CFT     := common/tests/coldfire
CCSRC   := src/vcard.c src/abook.c src/photo.c src/jpegdc.c common/src/png.c common/src/inflate.c third_party/tjpgd/tjpgd.c common/src/util.c \
           common/src/charset.c common/atari/libc.c $(CFT)/gemdos.c tests/coldfire/cfrun.c
CCFLAGS  = -Os -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -w -DCAL_TOS -DCFRUN -DPNG_SLIM -DPNG_STEPS -nostdinc -isystem $(GCCINC) \
           -Iatari/include -Icommon/atari/include -Isrc -Icommon/src -Ithird_party/tjpgd -Itests \
           -I$(CFT) -Itests/coldfire -ffunction-sections -fdata-sections
CFD     := $(OBJDIR)/corecf
# corecheck_rules(DIR, CPU flags, objects of its own, the link's libraries)
define corecheck_rules
$(1)/%.o: %.c $$(HDRS) tests/coldfire/cfstdio.h
	@mkdir -p $$(dir $$@)
	$$(CC) $(2) $$(CCFLAGS) -c -o $$@ $$<
$(1)/tests/corecheck.o: tests/corecheck.c tests/host/vcdump.c $$(HDRS) tests/coldfire/cfstdio.h
	@mkdir -p $$(dir $$@)
	$$(CC) $(2) $$(CCFLAGS) -include tests/coldfire/cfstdio.h -c -o $$@ $$<
$(1)/rt.o: $$(CFT)/rt.S
	@mkdir -p $$(dir $$@)
	$$(CC) $(2) -c -o $$@ $$<
$(1)/corecheck: $(1)/tests/corecheck.o $$(addprefix $(1)/,$$(CCSRC:.c=.o)) $(1)/rt.o $(3)
	$$(CC) $(2) -nostdlib -static -no-pie -Wl,-z,noexecstack -Wl,--gc-sections -o $$@ $$(filter %.o,$$^) $(4)
endef
$(eval $(call corecheck_rules,$(CFD)/cf,-mcpu=5475,$(CFD)/cf/common/atari/ll64.o,))
$(eval $(call corecheck_rules,$(CFD)/st,-m68000 -fno-store-merging -fdisable-tree-bswap,$(CFD)/st/common/atari/tos.o,))
# the list: the sample cards, the fuzz seeds (cards and JPEGs), vcgen.py's
$(CFD)/list: tests/host/vcgen.py tests/hatari/sample.vcf $(wildcard tests/fuzz/seeds/vcard/* tests/fuzz/seeds/jpeg/*)
	@mkdir -p $(CFD)/cards
	python3 tests/host/vcgen.py $(CFD)/cards 300 >/dev/null
	{ echo tests/hatari/sample.vcf; ls tests/fuzz/seeds/vcard/* tests/fuzz/seeds/jpeg/* tests/fuzz/found/jpeg/* \
	    2>/dev/null; find $(CFD)/cards -type f | sort | head -n 300; } > $@
core-cf: $(CFD)/cf/corecheck $(CFD)/st/corecheck $(CFD)/list
	qemu-m68k -cpu m68000 $(CFD)/st/corecheck > $(CFD)/corecheck.st || { echo "the ST's corecheck failed"; exit 1; }
	qemu-m68k -cpu cfv4e $(CFD)/cf/corecheck > $(CFD)/corecheck.cf || { echo "the FireBee's corecheck failed"; exit 1; }
	cmp $(CFD)/corecheck.st $(CFD)/corecheck.cf
	@echo "corecheck, the FireBee as the ST: $$(tail -n 1 $(CFD)/corecheck.cf), $$(wc -l < $(CFD)/corecheck.cf) lines"

itest: $(OBJDIR)/card-cli
	tests/integration.sh

clean:
	rm -rf $(OBJDIR) $(BIN) dist

.PHONY: all help icons dist firebee core-cf test itest clean fuzz prof FORCE
