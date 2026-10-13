# EContact

**Highlights**

- Contacts for the Atari ST, STE, TT and Falcon that look and work like the Mac's Contacts
- iCloud and other CardDAV address books (Fastmail, Nextcloud, Radicale, Synology...), two-way sync; On My Atari address books
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
- **The card**: a round picture with the initials, the name, the job and
  company, then every phone, email, web page and address with its label in
  grey ("mobile", "iPhone", "home", "work", "home fax", or your own), the
  birthday and the note. Click a value to copy it, or to write to an
  address in EMail; the **mail** button does the same.
- **Search** as you type: names, companies, emails, phone numbers (with or
  without spaces and dashes), addresses and notes.
- **Editing**: + and Edit at the bottom of the card, as on the Mac. The
  card's parts are sections of one dialog (Name, Phone, Email, Address,
  More) so it fits an ST's screen; labels come from a pop-up or Custom...
  What EContact doesn't edit (the photo, related names, social profiles,
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

## Getting started

1. Copy `ECONTACT.ACC` (Falcon/TT) or `ECONTST.ACC` (ST/STE, renamed to
   `ECONTACT.ACC` or not) into the root of your boot drive, e.g. `C:\`.
   For Falcon mode, make a folder `C:\ECONTACT` and put `CACERT.PEM` and
   `ROOTS.DAT` in it (EContact keeps its own files there too). The folder
   can be anywhere: `C:\ECONTACT.INF` says where in `FOLDER=`.
2. For an ST, set up ECal's gateway on a Raspberry Pi
   (`sudo gateway/install.sh` in ECal), or use the one you have.
3. Reboot, choose **Contacts** in the Desk menu, and add your iCloud
   account with an [app-specific password](https://support.apple.com/en-us/102654).

The [user guide](docs/GUIDE.md) describes the rest.

## Building

```
sudo apt install gcc-m68k-linux-gnu      # Debian/Ubuntu
make                                     # binary/ECONTACT.ACC, ECONTACT.PRG, ECONTST.ACC
```

As in ECal and EMail, the program is freestanding (`atari/`, no MiNTLib),
`tools/elf2tos.py` writes the TOS executable, one binary is both the
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
tests/    unit tests, CardDAV integration tests, Hatari screenshots
```

## Tests

```
make test       # the core: vCards from iCloud, 4.0, 2.1, labels, birthdays,
                # groups, writing back without losing anything (122 checks)
make itest      # card-cli against a real CardDAV server (Radicale): sync, add,
                # change, groups both ways, import, export, delete, and the
                # people list EMail and ECal read
GATEWAY=../ECal/gateway/ecalgate.py make itest    # the same through the Pi gateway
tests/hatari/docs_shots.py WORK etos512us.img     # the screenshots, in Hatari
```
