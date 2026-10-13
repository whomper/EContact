# EContact - contacts for the Atari ST/STE/TT/Falcon, in C, a desk
# accessory like ECal and a companion to EMail.
#
#   make            builds binary/ECONTACT.ACC (the desk accessory), ECONTACT.PRG
#                   (the same program, to start from the desktop) and ECONTST.ACC
#                   (the ST edition, through the Pi gateway) with a stock m68k ELF
#                   cross compiler (Debian/Ubuntu: apt install gcc-m68k-linux-gnu).
#                   No MiNTLib: the program is freestanding and tools/elf2tos.py
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
           -DCAL_TOS -nostdinc -isystem $(GCCINC) -Iatari/include -Isrc -Iatari \
           -Ithird_party/bearssl/inc -Ithird_party/tjpgd
LDFLAGS := -m68000 -nostdlib -static -no-pie -Wl,--emit-relocs -Wl,-T,atari/link.ld \
           -Wl,--build-id=none -Wl,-z,noexecstack -Wl,--no-warn-rwx-segments

# BearSSL 0.6 for Falcon mode, built for the 68030 (the rest stays 68000 code)
BSSL    := third_party/bearssl
BSSLSRC := $(wildcard $(BSSL)/src/*/*.c)
BSSLDEF := -DBR_USE_URANDOM=0 -DBR_USE_GETENTROPY=0 -DBR_USE_UNIX_TIME=0 \
           -DBR_USE_WIN32_TIME=0 -DBR_USE_WIN32_RAND=0 -DBR_RDRAND=0
BSSLCF  := -m68030 -O2 -fomit-frame-pointer -ffreestanding -fno-builtin -fno-pic -fno-pie \
           -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -w \
           -nostdinc -isystem $(GCCINC) -Iatari/include -I$(BSSL)/inc -I$(BSSL)/src $(BSSLDEF)
BSSLOBJ  = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/bssl/%.o,$(BSSLSRC))
HBSSLOBJ = $(patsubst $(BSSL)/src/%.c,$(OBJDIR)/hbssl/%.o,$(BSSLSRC))

CORE    := util charset conn tls bidi secret futil ctime xml inflate http vcard carddav abook photo jpegdc
ATARI   := libc tos plat_tos net_tos sting gem dsprsa
UI      := main win draw popup dsp sidebar list card editor dialogs field about font

OBJDIR  := build
OBJS0   := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/tjpgd.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(CORE) $(ATARI) $(UI)))
OBJS    := $(OBJS0) $(OBJDIR)/version.o
HDRS    := $(wildcard src/*.h atari/*.h atari/include/*.h ui/*.h dsp/*.h third_party/tjpgd/*.h)

# what goes onto the Atari: binary/
BIN     := binary

all: $(BIN)/ECONTACT.ACC $(BIN)/ECONTACT.PRG $(BIN)/ECONTST.ACC $(BIN)/ROOTS.DAT

# the ST edition: no TLS of its own (it uses the Pi gateway), 150 KB
# smaller; only BearSSL's MD5 (for Digest logins), as 68000 code
STBSSL  := hash/md5 codec/dec32le codec/enc32le
STOBJS  := $(OBJDIR)/crt0.o $(OBJDIR)/sting_s.o $(OBJDIR)/nf.o $(OBJDIR)/tjpgd.o \
           $(addprefix $(OBJDIR)/,$(addsuffix .o,$(filter-out tls,$(CORE)) $(ATARI) $(UI) tls_none)) \
           $(addprefix $(OBJDIR)/st/,$(addsuffix .o,$(notdir $(STBSSL)))) $(OBJDIR)/version.o

$(BIN)/ECONTST.ACC: $(OBJDIR)/econtst.elf tools/elf2tos.py
	PRGFLAGS=7 python3 tools/elf2tos.py $< $@

$(OBJDIR)/econtst.elf: $(STOBJS) atari/link.ld
	$(CC) $(LDFLAGS) -o $@ $(STOBJS) -lgcc

$(OBJDIR)/st/%.o: $(BSSL)/src/hash/%.c
	@mkdir -p $(dir $@)
	$(CC) $(subst -m68030,-m68000,$(BSSLCF)) -c -o $@ $<
$(OBJDIR)/st/%.o: $(BSSL)/src/codec/%.c
	@mkdir -p $(dir $@)
	$(CC) $(subst -m68030,-m68000,$(BSSLCF)) -c -o $@ $<

# one program, two names: it sees at start-up whether TOS loaded it as
# an accessory or started it as an application
$(BIN)/ECONTACT.ACC: $(OBJDIR)/contact.elf tools/elf2tos.py
	PRGFLAGS=7 python3 tools/elf2tos.py $< $@
$(BIN)/ECONTACT.PRG: $(BIN)/ECONTACT.ACC
	cp $< $@

$(OBJDIR)/contact.elf: $(OBJS) $(OBJDIR)/libbearssl.a atari/link.ld
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
# ChaN's JPEG decoder for the photos: 68000 code, for speed
$(OBJDIR)/tjpgd.o: third_party/tjpgd/tjpgd.c third_party/tjpgd/tjpgd.h third_party/tjpgd/tjpgdcnf.h | $(OBJDIR)
	$(CC) $(subst -Os,-O2,$(CFLAGS)) -w -c -o $@ $<

$(OBJDIR)/crt0.o: atari/crt0.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/sting_s.o: atari/sting.S | $(OBJDIR)
	$(CC) -m68000 -c -o $@ $<
$(OBJDIR)/nf.o: atari/nf.S | $(OBJDIR)
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
              -Wno-format-truncation -Isrc -Ithird_party/tjpgd -fsanitize=address,undefined
HOSTSRC    := $(addprefix src/,$(addsuffix .c,$(CORE))) third_party/tjpgd/tjpgd.c tests/host/plat_posix.c tests/host/version_host.c
HOSTLIB    := $(OBJDIR)/libbearssl-host.a

$(OBJDIR)/hbssl/%.o: $(BSSL)/src/%.c
	@mkdir -p $(dir $@)
	$(HOSTCC) -O2 -w -I$(BSSL)/inc -I$(BSSL)/src -c -o $@ $<

$(HOSTLIB): $(HBSSLOBJ)
	rm -f $@
	ar rcs $@ $^

$(OBJDIR)/card-cli: tests/host/card_cli.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/host/card_cli.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/people-cli: tests/host/people_cli.c share/people.c share/people.h $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -Ishare -I$(BSSL)/inc -o $@ tests/host/people_cli.c share/people.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/unit: tests/unit.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tests/unit.c $(HOSTSRC) $(HOSTLIB)

$(OBJDIR)/mkroots: tools/mkroots.c $(HOSTSRC) $(HDRS) $(HOSTLIB) | $(OBJDIR)
	$(HOSTCC) $(HOSTCFLAGS) -I$(BSSL)/inc -o $@ tools/mkroots.c $(HOSTSRC) $(HOSTLIB)

$(BIN)/ROOTS.DAT: $(BIN)/CACERT.PEM
	$(MAKE) $(OBJDIR)/mkroots
	$(OBJDIR)/mkroots $(BIN)/CACERT.PEM $@

test: $(OBJDIR)/unit
	$(OBJDIR)/unit

itest: $(OBJDIR)/card-cli $(OBJDIR)/people-cli
	tests/integration.sh

clean:
	rm -rf $(OBJDIR) $(BIN)/ECONTACT.ACC $(BIN)/ECONTACT.PRG $(BIN)/ECONTST.ACC

.PHONY: all test itest clean
