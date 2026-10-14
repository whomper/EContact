# EContact

**Highlights**

- Contacts for the Atari ST, STE, TT and Falcon that look and work like the Mac's Contacts
- iCloud and other CardDAV address books (Fastmail, Nextcloud, Radicale, Synology...), two-way sync; On My Atari address books
- Contacts' photos, from the card or from iCloud's links, dithered to whatever screen you have
- Groups, as iCloud keeps them (group cards) and as Nextcloud does (categories)
- The address book of [EMail](https://github.com/whomper/EMAIL) and [ECal](https://github.com/whomper/ECal): pick people to write to and to invite
- Desk accessory or program; Falcon mode (TLS 1.2 on the Falcon, RSA on the DSP) or any Atari through the Raspberry Pi gateway
- Hebrew throughout: names and notes right to left, the Israeli keyboard (F10)
- Works offline from its own copy; vCard import and export

A contacts program for the Atari, written in C for GEM. Like ECal it runs
as a **desk accessory**, so it opens from the Desk menu over whatever
program you're in, and it is laid out like the Mac's Contacts: the groups
on the left, the people in the middle under a search field, the chosen
person's card on the right. 

![EContact on a Falcon](docs/window.png)

- **The list**, sorted by last name under letter headings, the first name
  plain and the last name bold, the chosen one in a blue bar, as on the
  Mac. Settings sorts by first name or shows "Last, First".
- **The card**: the contact's photo in a circle (or the initials in grey
  when there is none), the name, the job and company, then every phone,
  email, web page and address with its label in grey ("mobile", "iPhone",
  "home", "work", "home fax", or your own), the birthday and the note.
  Click a value to copy it, or to write to an address in EMail; the
  **mail** button does the same.
- **Photos**: the JPEG in the card (as the Mac and iPhone write it) or
  behind a link (as iCloud often gives it), cropped as you cropped it on
  the iPhone, dithered to the screen's own colours on a Falcon or TT and
  to black and white on an ST. Syncing leaves them on the server: a photo
  is fetched when its card is opened, or all of them at once with **Sync
  Contact Photos...** (a progress bar, the time left, Cancel, and it goes
  on where it stopped). Decoded once (a second or two on a Falcon, about
  four on an 8 MHz ST, as measured in Hatari) and kept in
  `CACHE\PHOTOS`; a photo changed on the iPhone is noticed and decoded
  again (see below).
- **Search** as you type: names, companies, emails, phone numbers (with or
  without spaces and dashes), addresses and notes.
- **Editing**: + and Edit at the bottom of the card, as on the Mac. The
  card's parts are sections of one dialog (Name, Phone, Email, Address,
  More) so it fits an ST's screen; labels come from a pop-up or Custom...
  What EContact doesn't edit (the photo itself, related names, social profiles,
  Apple's own fields) is kept as it was.
- **Groups**: All Contacts, each account, its groups. New Group, Add to
  Group, Remove from Group, rename, delete; Send Email to Group gives EMail
  everyone in it. iCloud's group cards and Nextcloud's categories both
  work. A contact made while a group is shown goes into it.
- **iCloud** and any **CardDAV** server: address books found
  automatically, changes made elsewhere fetched with ETags so only what
  changed comes over the line, saves checked with If-Match so a change
  made on the iPhone meanwhile isn't overwritten.
- **On My Atari** address books, kept only on this computer.
- **vCard files**: import (3.0, 4.0 and the 2.1 of old phones, with
  quoted-printable and any charset; written to the server as 3.0) and
  export, of one card, a group, a book or the search results.
- **My Card**, as on the Mac; the window opens on it.
- Fonts (the system font or any monospaced GDOS font), the window and
  everything else remembered in `ECONTACT.INF`, which explains itself.
- Runs on TOS 1.04 to 4.x, EmuTOS, MagiC and MiNT, with STinG or MiNTnet,
  in ST high, ST medium, TT and Falcon resolutions; in TT/Fast RAM where
  there is some.

![Editing a card](docs/edit.png) ![ST high](docs/st-mono.png)

## EMail and ECal

EContact is the address book of the other two:

- **EMail**: ^B in a new message offers EContact's people and groups (and
  the addresses EMail remembers). Type the beginning of a name first,
  "gr", and ^B finds Grace; one match is filled in at once. EContact's
  **mail** button, or a double-click on an address, opens a new message
  to that person in EMail.
- **ECal**: an event has **Invitees**; **Add...** picks people or a whole
  group from EContact (or type addresses). iCloud and other CalDAV servers
  that do scheduling (RFC 6638) send the invitations by email. An
  invitation from someone else shows who sent it.
- Both have **Contacts...** in their menu to open EContact's window.

![EMail's ^B](docs/email-complete.png) ![ECal's invitations](docs/ecal-invitees.png)

The changes to EMail and ECal are in [integration/](integration) as
patches, with [docs/INTEGRATION.md](docs/INTEGRATION.md) describing how
the three talk (a file, `CONTACTS.TXT`, and two GEM messages), so other
programs can use EContact too.

## Photos and their cache

Photos are most of an address book: 500 contacts with photos are about
12 MB of vCards, half a megabyte without them. So:

- **Syncing leaves the photos on the server.** Cards are asked for with
  CardDAV's partial retrieval (RFC 6352 8.4): only the properties EContact
  shows, and `PHOTO` without its value, which says there is a photo but
  not what. The card is kept as fetched in part. A server that ignores the
  request (Radicale does) sends whole cards, and their photos are kept as
  they come; one that refuses it gets the plain request.
- **A photo is fetched when its card is opened**: the whole card, in one
  request. **Sync Contact Photos...** in the menu does it for everyone,
  a few cards a request, with a progress box (who, how many, the time
  left, Cancel). Stopped half way, it goes on from there the next time.
- **Before a card fetched in part is changed** (edited, put in a group)
  it is fetched whole, and the change is made to that, so nothing the
  server has (the photo, related names, social profiles) is lost.
- **Answers are asked for in sizes that fit in memory**: as many cards at
  a time as fit in an eighth of the memory (the first answer tells how big
  they are), halved when one is too big anyway; a card too big even alone
  comes without its photo.
- **The cache keeps photos apart**: `CACHE\BOOKnnnn.DAT` has the cards with
  a stand-in for each photo (`X-ECONTACT-PHOTO:` and the photo's
  fingerprint), `BOOKnnnn.PHO` the photos. Start-up reads only the cards:
  in Hatari, 500 contacts with photos open in 5 seconds on a Falcon (it was
  3½ minutes with the photos in the cards) and 32 on an 8 MHz ST. EContact
  1.9's cache is converted once, the first time it is read (72 seconds for
  those 500 on a Falcon). The stand-in never leaves the Atari.

A photo is decoded with ChaN's TJpgDec
([third_party/tjpgd](third_party/tjpgd)) at 1/2, 1/4 or 1/8 of its size
(1/8 needs no IDCT, which is what makes it quick on a 68000), cropped to a
square and averaged into a 64x64 colour picture. The JPEGs TJpgDec won't
take (progressive ones, the "extended" SOF1, 4:4:0 sampling, unusual
table layouts, CMYK) are read by `src/jpegdc.c` from each 8x8 block's
average alone: a progressive JPEG's first scan has them all, so the rest
of the file isn't read. PNG and HEIC aren't shown; the reason for each
photo that isn't is in the status line, the closing alert and the log. That picture is kept in
`CACHE\PHOTOS\Pxxxxxxx.PIC` (8 KB) and dithered for the screen when shown.

Keeping it right:

- every cached picture records a fingerprint of the card's whole `PHOTO`
  line (shifts and exclusive-ors: a 68000 multiplies in software). When the
  photo is changed anywhere (the iPhone, the Mac, icloud.com), the line
  changes, the fingerprint no longer matches, and the picture is made
  again the next time the card is shown. A card that was fetched whole and
  has changed is fetched whole again when syncing, so its new photo is seen;
- a photo given as a link can change while the link stays the same. So the
  cache also records the card's ETag and the picture's: when the card has
  changed, the picture is asked for again with `If-None-Match`, and a
  "304 Not Modified" keeps the cached one, a new picture replaces it;
- a photo taken off a card takes its picture with it, and after each
  refresh the pictures of contacts that are gone are removed.

Pictures are read a quarter of a second after a contact is chosen, so the
arrow keys go down the list without waiting; the initials show until the
photo is there. Offline, a cached photo still shows. Settings > General >
Show contacts' photos turns them off. The account's password is sent only
to the account's own server (or iCloud's photo servers), never to a link
elsewhere.

## How many contacts

Measured in Hatari on an 8 MHz Mega ST under EmuTOS, with `ECONTST.ACC`
and cards like iCloud's (a name, a company, one or two emails, two phones,
some with an address, birthday or note):

| ST | EContact's memory | Contacts | Start-up |
|----|-------------------|----------|----------|
| 1 MB | 250 KB | about 490 | 10 s for 490 |
| 2 MB | 750 KB | about 1,500 | about 30 s |
| 4 MB | 1.8 MB | about 3,700 | 45 s for 2,500 |

(1 MB and the 4 MB start-up measured; the rest worked out from the size
of a contact.)

- **About half a kilobyte a contact** in memory (photos stay on disk). The
  accessory takes half of the memory free at boot, leaving the other half
  to the programs you run; `HEAPKB=` in `ECONTACT.INF` sets another amount.
  A sync needs some room besides (the answers it reads, a few to an
  eighth of the memory at a time), so keep a fifth of it free.
- With more in the address book than fit, a few less show: each card in
  the cache keeps about 125 bytes in memory either way (1,000 on a 1 MB
  ST: 300 shown).
- **Contacts that don't fit are never left out quietly**: the window says
  "N not shown" and an alert gives the `HEAPKB=` to set. `CONTACTS.TXT` is
  then left as it was, so EMail and ECal keep everyone.
- **Start-up reads parsed contacts** from `CACHE\BOOKnnnn.IDX`: a card is
  parsed (about 85 ms on an 8 MHz ST) only the first time, and after it
  changes; the rest take about 18 ms each. The first start of a new
  version parses everything once (2,500 contacts: 4 minutes on an ST).
- **Syncing a big address book** reads the server's list of cards as it
  arrives when memory is small, keeping only what needs fetching.
- STinG takes 100 KB or more of its own: with it, a 1 MB ST holds about
  400 contacts. A 512 KB ST can't run EContact (the accessory alone is
  400 KB: 196 KB of program, 157 KB of buffers, its stack).

## How it reaches the servers

Contacts servers want HTTPS. EContact uses ECal's code and ECal's way:

```
ST, STE, any Atari:  Atari (ECONTST.ACC) --plain HTTP, home LAN--> Raspberry Pi (ecalgate.py) --HTTPS--> iCloud
Falcon, TT:          Falcon (ECONTACT.ACC, TLS 1.2, RSA on the DSP) ---------------------HTTPS--> iCloud
```

- **Through the Pi**: ECal's gateway (`gateway/` in
  [ECal](https://github.com/whomper/ECal)) serves EContact as it is; if
  ECal already works through your Pi, type the same address and port in
  Settings, Connection.
- **Falcon mode**: TLS 1.2 on the Falcon or TT with BearSSL, the server's
  RSA signature checked on the DSP56001, as in EMail and ECal.

## The files

In the [`binary`](binary) folder:

| File           | For                                                               |
|----------------|-------------------------------------------------------------------|
| `ECONTACT.ACC` | Falcon and TT: the accessory with Falcon mode (TLS, DSP); also works through the Pi |
| `ECONTST.ACC`  | ST and STE: the same accessory without TLS (100 KB smaller), through the Pi |
| `ECONTACT.PRG` | the same as ECONTACT.ACC, to start from the desktop          |
| `CACERT.PEM`, `ROOTS.DAT` | the root certificates for Falcon mode                  |
| `ECONTACT.HYP` | the user guide for ST-Guide: in EContact's folder, the Help key opens it |
| `ECONTACT.REF` | its pages' names, for ST-Guide's index (ALL.REF): with the .HYP |
| `icons/`       | the desktop icon: `ECONTACT.RSC` (black-and-white and 16-colour), `ECONTACT.DEF` (its names, for resource editors), `ECONTACT.ICN` and its mask `ECONTMSK.ICN` |

**The desktop icon.** Copy the icons from `icons/ECONTACT.RSC` into your
desktop's icon file (`DESKICON.RSC`, `DESKCICN.RSC`) with a resource
editor; `ECONTACT.DEF` gives them their names there. For EmuTOS,
`tools/icon/add_to_emuicon.py emuicon.rsc EMUICON.RSC` writes a copy of
EmuTOS's icon file with EContact's added (it says which number), for
the root of the boot drive; then **Options > Install icon**.
`tools/icon/make_rsc.py` makes the files from `tools/icon/make_icon.py`.

## Getting started

1. Copy `ECONTACT.ACC` (Falcon/TT) or `ECONTST.ACC` (ST/STE, renamed to
   `ECONTACT.ACC` or not) into the root of your boot drive, e.g. `C:\`.
   For Falcon mode, make a folder `C:\ECONTACT` and put `CACERT.PEM` and
   `ROOTS.DAT` in it, and `ECONTACT.HYP` for the Help key (EContact keeps
   its own files there too). The folder
   can be anywhere: the settings, `C:\ECONTACT.INF`, say where in
   `FOLDER=`; start `ECONTACT.PRG` from the folder once, or show the
   accessory where it is when it asks.
2. For an ST, set up ECal's gateway on a Raspberry Pi
   (`sudo gateway/install.sh` in ECal), or use the one you have.
3. Reboot, choose **EContact** in the Desk menu, and add your iCloud
   account with an [app-specific password](https://support.apple.com/en-us/102654).

The [user guide](docs/GUIDE.md) describes the rest.

## Building

```
sudo apt install gcc-m68k-linux-gnu      # Debian/Ubuntu
make                                     # binary/ECONTACT.ACC, ECONTACT.PRG, ECONTST.ACC,
                                         # and ECONTACT.HYP when hcp is installed
make help HCP=/path/to/hcp               # ECONTACT.HYP alone
```

`binary/ECONTACT.HYP` is made from `docs/GUIDE.md` by `tools/mkhelp.py`
(from EMail) and HCP, ST-Guide's compiler, from
[hypview](https://github.com/th-otto/hypview); `make test` checks it
carries the program's version.

As in ECal and EMail, the program is freestanding (`atari/`, no MiNTLib),
`common/tools/elf2tos.py` writes the TOS executable, one binary is both the
accessory and the program, and every build counts the minor version up
(`VERSION`, `tools/bump.py`). Commit `VERSION` with the program.

## Source

```
src/      the contacts core, also built on Linux for the tests:
          vcard.c    vCard 2.1/3.0/4.0 in and out      carddav.c  CardDAV
          abook.c    accounts, books, caches, sync, saving, groups, CONTACTS.TXT
          (http, xml, tls, conn, charset, bidi, secret, ctime: from ECal and EMail)
ui/       the GEM program: main.c, win.c (the window), sidebar.c, list.c,
          card.c, editor.c, dialogs.c; field.c, font.c, draw.c from ECal
share/    people.c: EContact's people for other programs (EMail, ECal)
integration/  the patches for EMail and ECal
atari/    TOS runtime, STinG, DSP (from ECal)
common/   code shared with the other AERA programs (from AERA-Atari):
          BearSSL, the DSP RSA program, bidi, dates, passwords, build tools
tests/    unit tests, CardDAV integration tests, Hatari screenshots
```

## Tests

```
make test       # the core: vCards from iCloud, 4.0, 2.1, labels, birthdays,
                # groups, writing back without losing anything, the parsed
                # cache (233 checks, photos of every kind of JPEG too)
make itest      # card-cli against a real CardDAV server (Radicale): sync, add,
                # change, groups both ways, import, export, delete, the
                # people list EMail and ECal read, photos and their cache,
                # partial retrieval (tests/host/partialproxy.py), little
                # memory, and converting 1.9's cache
GATEWAY=../ECal/gateway/ecalgate.py make itest    # the same through the Pi gateway
tests/hatari/docs_shots.py WORK etos512us.img     # the screenshots, in Hatari
tests/hatari/placement.py WORK etos512us.img      # where the accessory and the program
                                                  # find their settings and folder
tests/hatari/acc_reread.py WORK etos512us.img     # the accessory after the program changed
                                                  # the files: read again
tests/hatari/help_key.py WORK etos512us.img       # the Help key, with a stand-in for
                                                  # ST-Guide (make build/fakeguide.acc)
```

## Licence

EContact is free software: you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free
Software Foundation, either version 2 of the License, or (at your option)
any later version. It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See [COPYING](COPYING).

Copyright (C) 2026 Erez Yaary.

It includes, under their own licences (compatible with the GPL):

- [BearSSL](common/third_party/bearssl) by Thomas Pornin: MIT licence
  ([LICENSE.txt](common/third_party/bearssl/LICENSE.txt))
- [TJpgDec](third_party/tjpgd) by ChaN: free use, the notice kept (at the
  top of [tjpgd.c](third_party/tjpgd/tjpgd.c))
- `CACERT.PEM`, the root certificates for Falcon mode, from Mozilla's CA
  list (Mozilla Public License 2.0)
