# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EContact - contacts for the Atari ST/STE/TT/Falcon, in C, a desk
# accessory like ECal and a companion to EMail.
#
#   make            builds binary/ECONTACT.ACC (the desk accessory) and ECONTACT.PRG
#                   (the same program, to start from the desktop), for every Atari,
#                   with a stock m68k ELF cross compiler (Debian/Ubuntu: apt install
#                   gcc-m68k-linux-gnu), and copies TLS.OVL beside them.
#                   No MiNTLib: the program is freestanding and common/tools/elf2tos.py
#                   writes the TOS header.
#   make test       builds the contacts core for this computer and runs the
#                   unit tests; `make itest` also syncs with a local CardDAV
#                   server (Radicale).
#   make help       binary/ECONTACT.HYP, the guide for ST-Guide (needs hcp:
#                   common/tools/build-hcp.sh builds it)
#   make dist       a release's files in dist/: each file, and ECONTACT-x.y.zip
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
# `make EXTRA_CFLAGS=-DAES_PROFILE`: stretches between event calls over 50 ms
# to the emulator's console (common/atari/gem.c)
CFLAGS  += $(EXTRA_CFLAGS)
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,common/atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

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
BSSLST  := -m68000 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
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
TLSOVL  := common/tlsovl/TLS.OVL

CORE    := util charset conn ovl bidi secret futil ctime xml inflate http vcard carddav abook photo photopack jpegdc pfkeep
ATARI   := libc tos plat_tos net_tos sting gem tls_ovl bigbuf
UI      := main evwrap win draw popup panebar dsp_ovl sidebar list card editor dialogs field about font fontsel accmem help av av_people alert dialog aboutbox iconify clip dd mbar twin busy bgwork people_ask answer notify aboutline text

OBJDIR  := build
OBJS    := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/tjpgd.o $(OBJDIR)/crash.o $(OBJDIR)/crash_s.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI))) \
           $(addprefix $(OBJDIR)/bssl/,$(addsuffix .o,$(notdir $(STBSSL)))) $(OBJDIR)/version.o
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

all: $(BIN)/ECONTACT.ACC $(BIN)/ECONTACT.PRG $(BIN)/TLS.OVL $(BIN)/ROOTS.DAT $(if $(HAVEHCP),$(BIN)/ECONTACT.HYP)

help: $(BIN)/ECONTACT.HYP

# ECONTACT.REF (the pages' names, for ST-Guide's index) is written with it
$(BIN)/ECONTACT.HYP: docs/GUIDE.md tools/mkhelp.py VERSION docs/front.json docs/help/WINDOW.IMG \
                     common/help/ECONTACT.IMG common/tools/helpfront.py common/tools/gemimg.py | $(OBJDIR) $(BIN)
	python3 tools/mkhelp.py docs/GUIDE.md VERSION $(OBJDIR)/ECONTACT.STG
	python3 common/tools/helpfront.py page docs/front.json $(OBJDIR)/ECONTACT.STG $(OBJDIR)/ECONTACT.STG
	$(HCP) -q -o $@ $(OBJDIR)/ECONTACT.STG >/dev/null
	test -s $(BIN)/ECONTACT.REF

# one program, two names: it sees at start-up whether TOS loaded it as
# an accessory or started it as an application
$(BIN)/ECONTACT.ACC: $(OBJDIR)/contact.elf common/tools/elf2tos.py | $(BIN)
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@
$(BIN)/ECONTACT.PRG: $(BIN)/ECONTACT.ACC
	cp $< $@

$(OBJDIR)/contact.elf: $(OBJS) common/atari/link.ld
	$(CC) $(LDFLAGS) $(EVWRAP) -o $@ $(OBJS) -lgcc

$(BIN)/TLS.OVL: $(TLSOVL) | $(BIN)
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
# ChaN's JPEG decoder for the photos: 68000 code, for speed. Its IDCT
# overflows on a damaged photo's coefficients: -fwrapv (here and in every
# host build) makes that the wrap the 68000 does anyway, not undefined
# behaviour; its pixels are clipped through a masked table
TJPGD   := third_party/tjpgd/tjpgd.c third_party/tjpgd/tjpgd.h third_party/tjpgd/tjpgdcnf.h
$(OBJDIR)/tjpgd.o: $(TJPGD) | $(OBJDIR)
	$(CC) $(subst -Os,-O2,$(CFLAGS)) -fwrapv -w -c -o $@ $<

$(OBJDIR)/crt0.o: common/atari/crt0.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
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
HOSTCFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
              -Wno-format-truncation -Isrc -Icommon/src -Ithird_party/tjpgd -fsanitize=address,undefined
# the build machine's builds link tls.c, which answers tls_ovl_* too (7.4)
HOSTSRC    := $(foreach m,$(patsubst ovl,tls,$(CORE)),$(firstword $(wildcard src/$(m).c common/src/$(m).c))) tests/host/plat_posix.c tests/host/version_host.c
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
	$(HOSTCC) $(filter-out -fsanitize=%,$(HOSTCFLAGS)) -I$(BSSL)/inc -Wl,--wrap=malloc,--wrap=realloc \
		-o $@ tests/lowmem.c $(HOSTSRC) $(OBJDIR)/libbearssl-host.a $(OBJDIR)/plain-tjpgd.o

$(OBJDIR)/mkroots: common/tools/mkroots.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ common/tools/mkroots.c $(HOSTSRC) $(HOSTLIB)

# the root certificates: Mozilla's list, kept in common/
$(BIN)/CACERT.PEM: common/certs/CACERT.PEM | $(BIN)
	cp $< $@
$(BIN)/ROOTS.DAT: $(BIN)/CACERT.PEM
	$(MAKE) $(OBJDIR)/mkroots
	$(OBJDIR)/mkroots $(BIN)/CACERT.PEM $@

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
	  $(wildcard $(OBJDIR)/fakeguide.elf $(OBJDIR)/asker.elf $(OBJDIR)/vcbench.elf)
	@for f in $(FUZZERS); do \
	  $(OBJDIR)/replay_$$f tests/fuzz/seeds/$$f $$(ls -d tests/fuzz/found/$$f 2>/dev/null) || exit 1; \
	done
	@$(HYPCHECK)

# the help file says the program's version (when it was made)
HYPCHECK = test ! -f $(BIN)/ECONTACT.HYP || grep -q "ECONTACT.HYP $$(cat VERSION)" $(BIN)/ECONTACT.HYP || \
	  { echo "$(BIN)/ECONTACT.HYP isn't version $$(cat VERSION): make help"; exit 1; }

# a release's files: each one in dist/, and all of them in
# dist/ECONTACT-x.y.zip (the icons in icons/), for the release workflow
DIST    := $(BIN)/ECONTACT.ACC $(BIN)/ECONTACT.PRG $(BIN)/TLS.OVL $(BIN)/CACERT.PEM $(BIN)/ROOTS.DAT \
           $(BIN)/ECONTACT.HYP $(BIN)/ECONTACT.REF
ICONS   := icons/ECONTACT.RSC icons/ECONTNV.RSC icons/ECONTACT.DEF icons/ECONTACT.ICN icons/ECONTMSK.ICN
dist: all help
	@$(HYPCHECK)
	rm -rf dist
	mkdir -p dist/zip/icons
	cp $(DIST) $(ICONS) dist/
	cp $(DIST) dist/zip/
	cp $(ICONS) dist/zip/icons/
	cd dist/zip && zip -q -9 -r ../ECONTACT-$(VERSION_STR).zip .
	rm -rf dist/zip

itest: $(OBJDIR)/card-cli
	tests/integration.sh

clean:
	rm -rf $(OBJDIR) $(BIN) dist

.PHONY: all help icons dist test itest clean fuzz FORCE
