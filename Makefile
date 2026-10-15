# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EContact - contacts for the Atari ST/STE/TT/Falcon, in C, a desk
# accessory like ECal and a companion to EMail.
#
#   make            builds binary/ECONTACT.ACC (the desk accessory), ECONTACT.PRG
#                   (the same program, to start from the desktop) and ECONTST.ACC
#                   (the ST edition, through the Pi gateway) with a stock m68k ELF
#                   cross compiler (Debian/Ubuntu: apt install gcc-m68k-linux-gnu).
#                   No MiNTLib: the program is freestanding and common/tools/elf2tos.py
#                   writes the TOS header.
#   make test       builds the contacts core for this computer and runs the
#                   unit tests; `make itest` also syncs with a local CardDAV
#                   server (Radicale).
#
# The Atari runtime, TLS, HTTP, WebDAV and Hebrew text come from ECal and
# EMail (github.com/whomper/ECal, github.com/whomper/EMAIL).

CROSS   ?= m68k-linux-gnu-
CC      := $(CROSS)gcc
GCCINC  := $(shell $(CC) -print-file-name=include 2>/dev/null)
CFLAGS  := -m68000 -Os -fomit-frame-pointer -ffreestanding -fno-builtin \
           -fno-pic -fno-pie -fno-tree-loop-distribute-patterns -fno-store-merging \
           -fno-asynchronous-unwind-tables -Wall -Wno-pointer-sign \
           -DCAL_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Icommon/atari/include \
           -Isrc -Icommon/src -Iatari -Icommon/atari -Iui -Icommon/ui -Icommon/third_party/bearssl/inc \
           -DTLS_FULL_EDITION=\"ECONTACT.ACC\" -Ithird_party/tjpgd
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,common/atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

# BearSSL 0.6 for Falcon mode, built for the 68030 (the rest stays 68000 code)
BSSL    := common/third_party/bearssl
BSSLSRC := $(wildcard $(BSSL)/src/*/*.c)
BSSLDEF := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 \
           -DBR_USE_WIN32_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0
BSSLCF  := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -w \
           -nostdinc -isystem $(GCCINC) -Iatari/include -Icommon/atari/include -I$(BSSL)/inc -I$(BSSL)/src $(BSSLDEF)
BSSLOBJ  = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/bssl/%.o,$(BSSLSRC))
HBSSLOBJ = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/hbssl/%.o,$(BSSLSRC))

CORE    := util charset conn tls bidi secret futil ctime xml inflate http vcard carddav abook photo jpegdc
ATARI   := libc tos plat_tos net_tos sting gem dsprsa
UI      := main win draw popup dsp sidebar list card editor dialogs field about font help av alert aboutbox iconify clip dd mbar twin busy

OBJDIR  := build
OBJS0   := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/tjpgd.o $(OBJDIR)/crash.o $(OBJDIR)/crash_s.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI)))
OBJS    := $(OBJS0) $(OBJDIR)/version.o
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h \
           common/src/*.h common/atari/*.h common/atari/include/*.h common/dsp/*.h third_party/tjpgd/*.h)

# what goes onto the Atari: binary/
BIN     := binary

# the help file for ST-Guide, from docs/GUIDE.md, needs HCP (hcp, from
# github.com/th-otto/hypview; CLAUDE.md says how to build it): made with
# the program when hcp is there, else by `make help HCP=/path/to/hcp`.
# `make test` checks it says the program's version
HCP     ?= hcp
HAVEHCP := $(shell command -v $(HCP) 2>/dev/null)

all: $(BIN)/ECONTACT.ACC $(BIN)/ECONTACT.PRG $(BIN)/ECONTST.ACC $(BIN)/ROOTS.DAT $(if $(HAVEHCP),$(BIN)/ECONTACT.HYP)

help: $(BIN)/ECONTACT.HYP

# after the program: its build counts VERSION up. ECONTACT.REF (the
# pages' names, for ST-Guide's index) is written with it
$(BIN)/ECONTACT.HYP: docs/GUIDE.md tools/mkhelp.py VERSION $(BIN)/ECONTACT.ACC | $(OBJDIR)
	python3 tools/mkhelp.py docs/GUIDE.md VERSION $(OBJDIR)/ECONTACT.STG
	$(HCP) -q -o $@ $(OBJDIR)/ECONTACT.STG >/dev/null
	test -s $(BIN)/ECONTACT.REF

# the ST edition: no TLS of its own (it uses the Pi gateway), 150 KB
# smaller; only BearSSL's MD5 (for Digest logins), as 68000 code. Only
# ever an accessory, so no crash catcher (crash_none.c)
STBSSL  := hash/md5 codec/dec32le codec/enc32le
STOBJS  := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/tjpgd.o $(OBJDIR)/crash_none.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(filter-out tls,$(CORE)) $(ATARI) $(UI) tls_none)) \
           $(addprefix $(OBJDIR)/st/,$(addsuffix .o,$(notdir $(STBSSL)))) $(OBJDIR)/version.o

$(BIN)/ECONTST.ACC: $(OBJDIR)/econtst.elf common/tools/elf2tos.py
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@

$(OBJDIR)/econtst.elf: $(STOBJS) common/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(STOBJS) -lgcc

$(OBJDIR)/st/%.o: $(BSSL)/src/hash/%.c
	@mkdir -p $(dir $@)
	$(CC) $(subst -m68030,-m68000,$(BSSLCF)) -c -o $@ $<
$(OBJDIR)/st/%.o: $(BSSL)/src/codec/%.c
	@mkdir -p $(dir $@)
	$(CC) $(subst -m68030,-m68000,$(BSSLCF)) -c -o $@ $<

# one program, two names: it sees at start-up whether TOS loaded it as
# an accessory or started it as an application
$(BIN)/ECONTACT.ACC: $(OBJDIR)/contact.elf common/tools/elf2tos.py
	PRGFLAGS=7 python3 common/tools/elf2tos.py $< $@
$(BIN)/ECONTACT.PRG: $(BIN)/ECONTACT.ACC
	cp $< $@

$(OBJDIR)/contact.elf: $(OBJS) $(OBJDIR)/libbearssl.a common/atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(OBJDIR)/libbearssl.a -lgcc

$(OBJDIR)/bssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(BSSLCF) -c -o $@ $<

$(OBJDIR)/libbearssl.a: $(BSSLOBJ)
	rm -f $@
	$(CROSS)ar rcs $@ $^

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

$(OBJDIR):
	mkdir -p $@

# every build counts the version up (VERSION, major.minor as in EMail):
# version.c is written again
# whenever anything in either edition was compiled again
$(OBJDIR)/version.c: $(OBJS0) $(filter-out $(OBJDIR)/version.o,$(STOBJS)) src/version.h tools/bump.py
	python3 tools/bump.py VERSION $@
$(OBJDIR)/version.o: $(OBJDIR)/version.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---- host build: the same contacts core, for tests and card-cli ----
HOSTCC     ?= cc
HOSTCFLAGS := -O1 -g -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
              -Wno-format-truncation -Isrc -Icommon/src -Ithird_party/tjpgd -fsanitize=address,undefined
HOSTSRC    := $(foreach m,$(CORE),$(firstword $(wildcard src/$(m).c common/src/$(m).c))) tests/host/plat_posix.c tests/host/version_host.c
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

$(OBJDIR)/card-cli: tests/host/card_cli.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/host/card_cli.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/people-cli: tests/host/people_cli.c share/people.c share/people.h $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -Ishare -I$(BSSL)/inc -o $@ tests/host/people_cli.c share/people.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/unit: tests/unit.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/unit.c $(HOSTSRC) $(HOSTLIB)

# without the sanitizers: it refuses big mallocs itself (--wrap)
$(OBJDIR)/lowmem: tests/lowmem.c $(HOSTSRC) $(HDRS) $(OBJDIR)/libbearssl-host.a $(OBJDIR)/plain-tjpgd.o | $(OBJDIR)
	$(HOSTCC) $(filter-out -fsanitize=%,$(HOSTCFLAGS)) -I$(BSSL)/inc -Wl,--wrap=malloc,--wrap=realloc \
		-o $@ tests/lowmem.c $(HOSTSRC) $(OBJDIR)/libbearssl-host.a $(OBJDIR)/plain-tjpgd.o

$(OBJDIR)/mkroots: common/tools/mkroots.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ common/tools/mkroots.c $(HOSTSRC) $(HOSTLIB)

# the root certificates: Mozilla's list, kept in common/
$(BIN)/CACERT.PEM: common/certs/CACERT.PEM
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

test: $(OBJDIR)/unit $(OBJDIR)/lowmem $(addprefix $(OBJDIR)/replay_,$(FUZZERS))
	$(OBJDIR)/unit
	$(OBJDIR)/lowmem
	@for f in $(FUZZERS); do \
	  $(OBJDIR)/replay_$$f tests/fuzz/seeds/$$f $$(ls -d tests/fuzz/found/$$f 2>/dev/null) || exit 1; \
	done
	@grep -q "ECONTACT.HYP $$(cat VERSION)" $(BIN)/ECONTACT.HYP || \
	  { echo "$(BIN)/ECONTACT.HYP isn't version $$(cat VERSION): make help"; exit 1; }

itest: $(OBJDIR)/card-cli $(OBJDIR)/people-cli
	tests/integration.sh

clean:
	rm -rf $(OBJDIR) $(BIN)/ECONTACT.ACC $(BIN)/ECONTACT.PRG $(BIN)/ECONTST.ACC

.PHONY: all help test itest clean fuzz
