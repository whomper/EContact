# EContact - user guide

## Installing

EContact is a desk accessory: TOS loads it at boot from the root of the
boot drive, and it waits in the Desk menu as **Contacts** until you
choose it.

| Your Atari        | Copy to `C:\`    | Connects                        |
|-------------------|------------------|---------------------------------|
| Falcon, TT        | `ECONTACT.ACC`   | by itself (Falcon mode) or through the Pi |
| ST, STE, Mega ST  | `ECONTST.ACC`    | through the Raspberry Pi gateway |

The settings are in `ECONTACT.INF` in the root of the boot drive, where
the accessory, EMail and ECal can always find them. Everything else is in
EContact's folder, which can be anywhere: `FOLDER=` in `ECONTACT.INF`
says where.

```
C:\ECONTACT.ACC
C:\ECONTACT.INF           settings, accounts, address books; FOLDER= names the folder
C:\ECONTACT\              EContact's folder (or another one):
    ECONTACT.PRG          the program, if you use it
    ECONTACT.KEY          the key the passwords in ECONTACT.INF are encrypted with
    CACHE\                a copy of every address book (BOOKnnnn.DAT)
    CONTACTS.TXT          everyone's addresses and the groups, for EMail and ECal
    ECONTACT.FNT          which GDOS fonts are fixed-width (made by Font...)
    CACERT.PEM            Falcon mode: root certificates (copy them here)
    ROOTS.DAT             Falcon mode: the same, decoded ahead of time
    ECONTACT.LOG          with Write a log on
```

- **`ECONTACT.PRG`** is the same program, to start from the desktop or a
  shell; it ends when its window closes. It uses the folder it was
  started from as EContact's folder and writes that into `FOLDER=`, so
  starting it once from your folder is all it takes for the accessory to
  find it too. The accessory and the program share the settings and the
  folder: use either, or both (not both at the same moment).
- **The accessory** reads `FOLDER=` when it starts. If it's empty, or
  the folder isn't there any more, the first time you open Contacts it
  asks where the folder is: **Locate...** opens the file selector (pick
  any file in the folder), or use `C:\ECONTACT`. The answer is kept in
  `FOLDER=`.
- Settings kept in the folder instead (`C:\ECONTACT\ECONTACT.INF`, or
  `ECONTACT.INF` next to the program) are copied to the root the first
  time, when there are none there.

**Memory.** The accessory takes its memory once, at boot: 1.5 MB on a
Falcon or TT (4 MB from TT/Fast RAM where there is some), half of what's
free on an ST: about 250 KB on a 1 MB ST (some 490 contacts), 750 KB on
2 MB (1,500), 1.8 MB on 4 MB (3,700). `HEAPKB=` in ECONTACT.INF sets
another amount, in kilobytes (`ECONTACT.PRG` takes what it needs from
all that is free); a contact takes about half a kilobyte, and
a sync wants a fifth of the memory free besides. If contacts don't fit,
the window says how many aren't shown and an alert says what to set:

![Contacts that don't fit](memory-full.png)

**TT RAM.** On a TT, or a Falcon with an accelerator's Fast RAM, TOS
loads EContact into TT RAM, and its memory comes from there too, leaving
ST RAM to the programs that need it. The accessory then takes 4 MB (still
at most half of what's free). The About box says "TT RAM" when EContact
runs there, and with **Write a log** on, the log's first line says where
the program and its memory are.

## Accounts

The menu (the button at the top right) > **Add account...**:

- **iCloud**: your Apple ID and an **app-specific password** (make one at
  appleid.apple.com > Sign-In and Security > App-Specific Passwords; your
  own password doesn't work for other programs). EContact finds your
  address book at contacts.icloud.com.
- **Other CardDAV account**: the server's address (for instance
  `https://carddav.fastmail.com/`, `https://cloud.example.org/remote.php/dav`,
  or the address of one address book), user name and password. Google
  wants OAuth, which EContact can't do: export your contacts as a .vcf
  file at contacts.google.com and import it into an On My Atari book.
- **On My Atari**: an address book kept only on this computer.

Servers want a secure connection: in **Settings > Connection** either
type the address of the Raspberry Pi gateway (ECal's; the same address
and port as in ECal) or, on a Falcon or TT, turn on **Falcon mode**.

Right-click an account's name on the left to refresh it, change its
settings or delete it from this Atari (nothing is deleted on the server).

## The window

![The window](window.png)

- **On the left**: All Contacts, then each account with its address books
  ("All iCloud") and their groups. Click one to see those people. The
  button at the top left (or ^S) hides the list.
- **In the middle**: the people, sorted by last name under letter
  headings. Click one to see their card; the arrow keys move up and down,
  Clr/Home goes to the first (Shift: the last).
- **Search**: just type. The list keeps those whose name, company, email,
  phone number, address or note has what you typed ("0170 123" finds
  "0170 1234567"); Backspace takes a letter back, Esc ends the search.
- **On the right**: the card. Click a phone, address, birthday or note
  to copy it to the clipboard; click an email to copy it or write to it
  in EMail (a double-click writes at once). **mail** under the name writes
  to the person (asking which address when there are several).
- **At the bottom of the card**: **+** for a new contact and **Edit**.
- The info line says how many contacts are shown and when they were last
  updated.

![A group](group.png) ![Searching](search.png)

## Photos

A contact's photo shows in the circle at the top of the card, as on the
Mac, cropped the way it was cropped on the iPhone. Photos are in the cards
themselves, or a link to iCloud's photo server, which EContact fetches
with your account.

Syncing doesn't download them: they are most of an address book (500
contacts with photos are about 12 MB), and an ST's memory and a gateway's
link are small. The first time a card with a photo is shown, the photo is
fetched and decoded (the busy bee and "Getting the photo..."; a second or
two on a Falcon, about four seconds on an 8 MHz ST, and the time to
download it) and kept in `CACHE\PHOTOS`; after that it shows at once,
offline too.

To have them all, choose **Sync Contact Photos...** in the menu (the
button at the right of the toolbar). A box shows who is being done, how
many are left and about how long it will take. **Cancel** (or Esc) stops
after the one being done; what is done is kept, and choosing it again goes
on from there. Offline, it makes the pictures of the photos already
downloaded. Some servers send the photos while syncing anyway: those are
kept as they come, and Sync Contact Photos only has to decode them.

![Sync Contact Photos](photo-sync.png)

When someone's photo changes on another device, EContact sees it after the
next refresh and decodes the new one; a photo taken off a card goes, and
so do the pictures of deleted contacts.

On a Falcon or TT in 16 or 256 colours the photo is dithered to the
screen's colours; on an ST it is black and white. Settings > General >
**Show contacts' photos** turns them off (the initials show instead).
EContact shows JPEG photos, which is what iPhones and Macs write; the rare
PNG or progressive JPEG shows the initials.

## Changing a card

**Edit** (or a double-click, Return, ^E) opens the card's sections:

- **Name**: first, last, middle, nickname, prefix ("Dr."), suffix,
  company, department, job title; **Show as company** makes the company
  the card's name, as on the Mac.
- **Phone**, **Email**: six of each, each with its label: mobile, iPhone,
  home, work, main, home fax, work fax, pager, other, or **Custom...** for
  your own word.
- **Address**: two addresses (a third one from the Mac is kept as it is).
- **More**: the birthday as day.month.year ("17.05.1975", or "17.05"
  without the year), web pages, and the note.

**Done** saves to the server at once; **Delete** deletes the card (on the
server too). What EContact doesn't edit (the photo, related names,
social profiles, instant messaging, the iPhone's own fields) stays on the
card as it was. If the card was changed on another device in the
meantime, the server refuses the save: refresh (^R) and edit again.

Text fields take Hebrew: F10 switches to the Israeli keyboard, and a
Hebrew name or note is shown right to left.

## Groups

![The contact menu](context-menu.png)

- **New group**: the menu > New group (^G), or right-click an address book.
- **Add to Group...**: right-click a contact; the list ticks the groups it
  is in, and choosing a ticked one takes it out. A new contact made while
  a group is shown goes into that group.
- **Remove from (group)**: right-click a contact while the group is shown.
- Right-click a group to **Send Email to Group** (everyone in it, in
  EMail's To: line), export it, rename it (or double-click it) or delete
  it (the people stay).

iCloud keeps a group as a card of its own listing its members; Nextcloud
and some others put the group's name in each member's categories. Both
show and work the same way here; new groups are made as group cards.

## Import and export

- **Import vCards...** (the menu, or right-click an address book): a .vcf
  file with one or many cards, from a phone, Outlook, Google or another
  Mac. Old 2.1 cards are written as 3.0, which iCloud wants.
- **Export vCards...**: the chosen card, or all the list shows (an
  address book, a group, or what a search found). Cards whose photos
  weren't downloaded yet are fetched whole for it; offline they are
  exported as they are kept, without their photos.

## My Card

Right-click your own card > **Make This My Card**. The list marks it
"me", and the window opens on it.

## Settings

- **General**: sort by last or first name, show names as "First Last" or
  "Last, First", which address book new contacts go into, where the font
  has its Hebrew letters, starting with the Hebrew keyboard, and showing
  photos.
- **Refresh**: refresh all every 15 minutes, hour or day, or only by hand
  (^R, the Refresh button); and when the window opens.
- **Connection**: Falcon mode and the DSP, the Pi gateway, and the log.

The menu also has **Work offline** (no connections at all: everything
shown comes from the copy on disk; a server's address book can't be
changed until you're online again), **Font...**,
**Keys...** (Help) and **About**.

## With EMail and ECal

EContact writes `CONTACTS.TXT` in its folder whenever the contacts change:
every email address with its name, and the groups. With the patches in
`integration/` applied:

- **EMail**: in a new message, ^B (Message > Address book) lists
  EContact's people and groups, then the addresses EMail remembers. Type
  the beginning of a name on the To:, Cc: or Bcc: line first ("gr") and
  ^B shows only those; with one match it is filled in at once. A group
  puts all its members on the line.
- **ECal**: an event's **Invitees** line takes addresses; **Add...** picks
  people or a group from EContact, completing a name begun on the line.
  The invitations are written as the event's attendees, with you (the
  account's Apple ID or email user name) as organizer, and iCloud sends
  them. An invitation you received shows who it is from.
- **mail** in EContact opens a new message in EMail when EMail is
  running; when it isn't, the address goes to the clipboard.
- **Contacts...** in EMail's File menu and ECal's menu opens EContact's
  window.

See [INTEGRATION.md](INTEGRATION.md) for how the programs talk.

## ECONTACT.INF

The settings file explains itself, like ECal's and EMail's: a note
before each setting, `1` for on and `0` for off. It can be edited with
any text editor while EContact isn't running (keys in any case, a note
after a value with ` ;`). A password typed in there in plain text is
encrypted with ECONTACT.KEY the next time EContact starts.

## Keys

| Key | Does |
|-----|------|
| ↑ ↓ | the contact above, below |
| Clr/Home, Shift+Clr/Home | the first, the last |
| Return, ^E | edit the card |
| Delete | delete the card |
| ^N | a new contact |
| ^G | a new group |
| typing | search |
| Esc, ^F | a new search |
| ^R, Shift+^R | refresh, everything again |
| ^S | the groups on the left |
| F10 | Hebrew keyboard |
| ^W, ^Q | close the window (^Q also quits the program) |
| Help | the keys |
| Double-click | edit; on an email: write to it |
| Right-click | more choices |
