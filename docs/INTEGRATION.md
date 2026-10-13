# EContact and other programs

EContact is the address book of EMail and ECal, and any GEM program can
use it the same way. There are three pieces: a file to read, a message to
open EContact's window, and a message EContact sends to write to someone.

## CONTACTS.TXT: the people

EContact writes `CONTACTS.TXT` in its folder whenever the contacts change
(after a refresh, an edit, an import...). The folder is in
`ECONTACT.INF`, in the root of the boot drive, on the line `FOLDER=`
(near the top of the file, so it can be read without reading it all).

The file is Atari text (Hebrew at 0xC2-0xDC, in reading order), CRLF
lines; lines starting with `;` are notes:

```
P<tab>name<tab>email<tab>label                  one line for each email address
G<tab>group<tab>Name <email>, Name <email>...    one line for each group with addresses
```

```
P	Maya Carter	maya@pixelpress.example	work
P	Maya Carter	maya.carter@example.com	home
G	Atari Club	Grace Baker <grace.baker@example.net>, Daniel Hughes <dan@retrorepairs.example>
```

[`share/people.c`](../share/people.c) reads it: copy `people.c` and
`people.h` into a program's `src/` (they need only `util.c` and
`plat.h`, as EMail and ECal have them). It finds the file, lists the
people and groups matching what was typed, and completes a To: line:

```c
people_drive('A' + boot_drive());          /* once, at start-up */
...
char part[100], *line2;
PPICK *l;
int n;
people_partial(line, part, sizeof(part));  /* "Bob <b@x>, gr" -> "gr" */
n = people_pick_list(part, &l);            /* groups first, then by name */
/* show l[i].label, let the user choose k */
line2 = people_complete(line, l[k].value); /* "Bob <b@x>, Grace Baker <g@x>" */
people_pick_free(l, n);
```

## Opening EContact's window

Send EContact the message `0x4543` ('EC') with `appl_write`; it opens (or
tops) its window. The accessory's AES name is `ECONTACT` (or `ECONTST `
for the ST edition, renamed or not):

```c
short id = appl_find("ECONTACT"), msg[8] = { 0x4543, ap_id, 0, 0, 0, 0, 0, 0 };
if (id < 0)
	id = appl_find("ECONTST ");
if (id >= 0)
	appl_write(id, 16, msg);
```

`AC_OPEN` with -1 as the menu id does the same.

## Writing to someone

EContact's **mail** button sends the AV protocol's `VA_START` (`0x4711`)
to the program named `EMAIL`, with a pointer in words 3 and 4 to
`mailto:Maya Carter <maya@pixelpress.example>` (or several addresses
separated by commas, for a group). The text is in memory any program may
read (global, under MiNT's memory protection) and stays there. EMail
opens a new message with it on the To: line; up to a `?`, as in a
`mailto:` link, is the address.

## Invitations in ECal

ECal keeps an event's invitees in its `ATTENDEE` lines with this user as
`ORGANIZER`, as RFC 5545 has them; a CalDAV server that schedules
(RFC 6638: iCloud, Fastmail, Nextcloud...) sends the invitations by
email and collects the answers. ECal writes them only when the invitees
were changed in its editor; otherwise the lines are kept exactly as they
came, so an event from an iPhone keeps everyone's answers.
