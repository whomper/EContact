# EContact: building and testing

## Building

```sh
sudo apt install gcc-m68k-linux-gnu python3 zip   # Debian/Ubuntu; no MiNTLib needed
make                     # binary/econtact.acc, econtact.prg, roots.dat, tls.ovl (common's,
                         # as it is), and econtact.hyp when hcp is installed
make help HCP=/path/to/hcp   # econtact.hyp alone
make dist                # a release's files in dist/: each file, and econtact-x.y.zip
make icons               # icons/, ui/icon*.h, docs/icon*.png from common/icons/ECONTACT.txt
```

`binary/econtact.hyp` is made from `docs/GUIDE.md` by `tools/mkhelp.py` (from EMail) and HCP,
ST-Guide's compiler, from [hypview](https://github.com/th-otto/hypview)
(`common/tools/build-hcp.sh DIR` builds it); `make test` checks it carries the program's version.

As in ECal and EMail, the program is freestanding (no MiNTLib), `common/tools/elf2tos.py` writes the
TOS executable, and one binary is both the accessory and the program. `binary/` and `dist/` are what
make builds; they are never committed, and a build changes no tracked file. Every file
EContact names, and every file a release carries, is in lowercase (the guidelines 6.6: a MiNT
ext2 drive tells `econtact.inf` from `ECONTACT.INF`).

**Versions and releases.** `VERSION` holds the last release, major.minor (e.g. 1.33), and the build
never changes it. Every build shows it as it is in the About box and the User-Agent EContact sends;
only releases leave the build machine. To release: `tools/bump.py`, commit and push, then
run the Release workflow (**Actions > Release > Run workflow**, on `main`) with what is new:
[`.github/workflows/release.yml`](../.github/workflows/release.yml) tags the commit `v1.34`, tests,
builds the programs and the help file, and publishes them as a GitHub Release with those notes.
Pushing a tag `v1.34` made with `git tag -a` does the same, with the tag's message as the notes.

## Source

```
src/          the contacts core, also built on Linux for the tests:
              vcard.c    vCard 2.1/3.0/4.0 in and out      carddav.c  CardDAV
              abook.c    accounts, books, caches, sync, saving, groups
              photo.c    photos and their cache            jpegdc.c   the JPEGs TJpgDec won't take
              http.c, xml.c, inflate.c: HTTP, WebDAV's XML and gzip, from ECal
ui/           the GEM program: main.c, win.c (the window), sidebar.c, list.c,
              card.c, editor.c, dialogs.c, about.c; answer.c: PEOPLE, EMail and ECal
              asking about people; field.c, font.c, draw.c from ECal
gateway/      the Raspberry Pi gateways (ecalgate.py serves EContact) and aera-gateway, their
              installer: a copy of AERA-Gateway's; change it there, then tools/sync-gateway.sh
atari/        plat_tos.c: files, folders and time on GEMDOS
common/       code shared with the other AERA programs (from AERA-Atari): the TOS
              runtime, STinG, the DSP, TLS and BearSSL, the GEM building blocks,
              bidi, dates, passwords, build tools
third_party/  ChaN's TJpgDec, for the photos
tests/        unit tests, fuzz targets, CardDAV integration tests, Hatari rigs
tools/        the version bump, the help file, EmuTOS's icon file, sync-common.sh, sync-gateway.sh
icons/        the desktop icon, as a release carries it
docs/         the user guide (GUIDE.md, the source of ECONTACT.HYP), INTEGRATION.md, pictures
```

## Tests

```
make test       # the core: vCards from iCloud, 4.0, 2.1, labels, birthdays,
                # groups, writing back without losing anything, the parsed
                # cache (240 checks, photos of every kind of JPEG too); the
                # fuzz inputs again (tests/fuzz/seeds, and found/: what once
                # broke a parser)
make fuzz       # CardDAV answers, vCards, JPEG photos, XML and inflate from
                # libFuzzer (clang), FUZZTIME seconds each
make itest      # card-cli against a real CardDAV server (Radicale: pip install
                # radicale): sync, add, change, groups both ways, import,
                # export, delete, the people list EMail and ECal read, photos
                # and their cache, partial retrieval (tests/host/partialproxy.py),
                # little memory, converting 1.9's cache, the index found by
                # where the cards are, the refresh in the background, and
                # PEOPLE's answers (card-cli ask)
GATEWAY=gateway/ecalgate.py make itest            # the same through the Gateway
tools/parser_diff.sh [REV]                        # vcard.c reads 8,000 made-up and broken cards
                                                  # as REV's did, byte for byte (tests/host/vcgen.py)
make build/VCBENCH.PRG && tests/hatari/vcbench.py WORK [MACHINE]
                                                  # how long a card takes to read on an Atari
tests/hatari/docs_shots.py WORK                   # the screenshots, in Hatari (EMail's and
                                                  # ECal's with EContact: the hub's family run)
tests/hatari/placement.py WORK [MACHINE]          # where the accessory and the program
                                                  # find their settings and folder
tests/hatari/acc_reread.py WORK [MACHINE]         # the accessory after the program changed
                                                  # the files: read again
tests/hatari/twin_test.py WORK [MACHINE]          # the program with the accessory installed
tests/hatari/help_key.py WORK [MACHINE]           # the Help key, with a stand-in for
                                                  # ST-Guide (make build/fakeguide.acc)
tests/hatari/stop_test.py WORK [MACHINE]          # Esc stops a server that says nothing
                                                  # (FAKESTNG and the serial bridge from common/)
make OBJDIR=WORK/build BIN=WORK/bin EXTRA_CFLAGS=-DAES_PROFILE \
     WORK/bin/econtact.acc                        # an edition that times its event calls
tests/hatari/quiet_start.py WORK [MACHINE]        # the accessory's quiet start: 600 contacts
                                                  # read in the background, at most 100 ms
                                                  # (ST) or 50 ms (TT, Falcon) between event calls
make OBJDIR=WORK/build BIN=WORK/bin EXTRA_CFLAGS=-DAES_PROFILE=20 \
     WORK/bin/econtact.prg WORK/bin/econtact.acc
tests/hatari/speed.py WORK [MACHINE...] [--nvdi] [--profile WORK/build/contact.elf]
                                                  # how long the user waits (the guidelines'
                                                  # Speed): 621 contacts on ST:mono, ST:med and
                                                  # Falcon-4:16, each action's longest stretch
                                                  # against its time; --profile: Hatari's
                                                  # profiler says where it went; the accessory
                                                  # only on Falcon or TT (--only acc)
tests/hatari/quiet_sync.py WORK [MACHINE]         # its refresh with the window closed, through
                                                  # the gateway (FAKESTNG and the bridge too)
tests/hatari/falcon_tls.py WORK [MACHINE] [--no-dsp]
                                                  # a Direct connection (TLS.OVL) against an HTTPS
                                                  # Radicale with an ECDSA certificate: the curves
                                                  # on the DSP, SIGS.DAT, ROOTS.DAT dated in place;
                                                  # without TLS.OVL it says so, no crash
tests/hatari/acc_files.py WORK [MACHINE] [cards]  # the accessory's refresh and Sync contact photos:
                                                  # no file open across an event call (2.2), from
                                                  # Hatari's GEMDOS and AES trace; what it wrote
                                                  # reads back whole
tests/hatari/esc_test.py WORK [MACHINE]           # Esc empties a field, Undo cancels (3.5)
tests/hatari/multi_select.py WORK [MACHINE]       # several chosen: Shift+click, Control+click, ^A,
                                                  # their menu, ^C, two deleted at once
tests/hatari/search_click.py WORK [MACHINE]       # a click gives the search field the focus
tests/hatari/long_field.py WORK [MACHINE]         # a field holds more than it shows: 52 letters
                                                  # in a field 30 wide, Shift+Left/Right to its ends
tests/hatari/list_bar.py WORK [MACHINE] [--acc] [--nvdi] [--flat]
                                                  # the list's own scroll bar: arrows and the
                                                  # track held, a page, the slider dragged; the
                                                  # rows moved match those drawn
tests/hatari/card_scroll.py WORK [MACHINE] [--flat]
                                                  # a long card in the window's own slider (5.1):
                                                  # arrows and track scroll it, the list stays
make build/alertacc.acc && tests/hatari/xaaes_dialogs.py WORK
                                                  # the dialogs in windows under FreeMiNT and
                                                  # XaAES (3.5): moved, covered, closed; alerts
tests/hatari/busy_keys.py WORK [MACHINE]          # ^N typed while busy still opens New Contact
                                                  # (3.2: key_ctrl, not kstate alone)
make build/ASKER.PRG && tests/hatari/people_ask.py WORK [MACHINE]
                                                  # PEOPLE: a stand-in for EMail asks for a name
                                                  # at boot and picks someone in the window
```

The Hatari rigs run under Xvfb, with xdotool for the mouse, on the family's test machines
(AERA-Atari's guidelines, The machines; `common/tools/machines.py list`): `ST` (Erez's 1040 ST,
4 MB), `ST-1` (1 MB), `TT` and `Falcon` (Erez's, with the DFB1: 32 MHz, 14 MB ST RAM, 128 MB TT
RAM), with a screen after a colon (`ST:mono`, `Falcon:256`). The accessory isn't run on a 4 MB
machine (`ST`, `Falcon-4`: Erez, 4 Nov 2026), which stay the program's: so MACHINE is the 1 MB ST
(`ST-1:mono`) by default for the rigs that load it, the Falcon in 16 colours for `twin_test.py`, in
256 for the screenshots, and `quiet_start.py` runs on `ST-1` and `Falcon`. machines.py gives each Hatari its arguments and its own
settings, and the TOS: EmuTOS 1.4, fetched and checked once, or Erez's own TOS when
`~/.aera/tos` has it. `quiet_sync.py`, `stop_test.py` and `falcon_tls.py` reach the network
through its FAKESTNG and serial bridge (common/tools/fakesting). MACHINE is the Falcon in 16
colours for `falcon_tls.py` too.

## Licence

EContact is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version. It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [COPYING](../COPYING).

Copyright (C) 2026 AERA | Erez Yaary.

It includes, under their own licences (compatible with the GPL):

- [BearSSL](../common/third_party/bearssl) by Thomas Pornin: MIT licence
  ([LICENSE.txt](../common/third_party/bearssl/LICENSE.txt))
- [TJpgDec](../third_party/tjpgd) by ChaN: free use, the notice kept (at the
  top of [tjpgd.c](../third_party/tjpgd/tjpgd.c))
- `CACERT.PEM`, the root certificates for a Direct connection, from Mozilla's CA
  list (Mozilla Public License 2.0)
