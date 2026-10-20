# EContact and other programs

EContact is the address book of EMail and ECal, and any GEM program can
use it the same way. There are three pieces: the PEOPLE message that
asks EContact about people, the message that opens EContact's window (or
imports vCards), and the message EContact sends to write to someone.

## PEOPLE: asking EContact about people

EMail and ECal show people by name and let the user pick them in
EContact's window. They ask EContact with the family's PEOPLE message
(0x4E43, the AERA guidelines 4.1); no program reads another's files.
PEOPLE is laid out as `VA_START` (word 1 the sender, words 3 and 4 a
string the receiver may read) and answered with `AV_STARTED`. Strings
are at most 1 KB of Atari text; a longer question or answer goes as
several whole ones, each sent after the last one's `AV_STARTED`.

```
names:EMAIL a@b c@d        ->  names:                       a line for each address,
                               a@b<tab>Grace Baker            an empty name when EContact
                               c@d<tab>                       doesn't know it
pick:ECAL gra              ->  EContact's window chooses (people, or a group's
                               members with an address; "gra" fills its search):
                               pick:
                               grace@example.net<tab>Grace Baker
                               (a bare pick: when cancelled, closed or quit)
changed:                   <-  to the askers, when the names or addresses changed,
                               and when the accessory has read its books
```

`EMAIL` and `ECAL` are the askers' AES names. EContact answers from the
contacts it holds, without reading a file or raising an alert; before
the accessory has read its books (in the background, a minute after
boot) the names are empty, and the askers are told `changed:` once it
has. With ECONTACT.PRG running, the accessory hands the question on to
it. The shared code does both sides (AERA-Atari's `common/ui/av.c` and
`common/ui/people_ask.c`; EContact's answers are `ui/answer.c`):

```c
people_init(64, 2048, names_came, picked);   /* once, at start */
...
case AV_PEOPLE: people_message(s, msg[1]);   /* in every event loop */
people_poll();                               /* each turn */
...
const char *n = people_name("grace@example.net");   /* 0 while asked */
people_pick("gra");                                 /* picked() gets them */
```

## Opening EContact's window, importing vCards

EContact takes the AV protocol's `VA_START` (`0x4711`), as every AERA
program does: with an empty string it opens (or tops) its window; with
the names of `.vcf` files it imports them into the address book new
contacts go into. The accessory's AES name is `ECONTACT` (or `ECONTST `
for the ST edition). With the AERA programs' shared code
(`common/ui/av.c`):

```c
if (!av_send("ECONTACT", "") && !av_send("ECONTST ", ""))
	alert(1, "[1][EContact isn't running.][ OK ]");
```

`av_send` keeps the string where EContact may read it (global memory
under MiNT's memory protection). `AC_OPEN` with -1 as the menu id also
opens the window. (The private message `0x4543` of earlier versions is
gone.)

## Writing to someone

EContact's **mail** button sends `VA_START` to the first of EMail's
editions running (`EMAIL`, or `EMAILST` on an ST), with a pointer in words 3 and 4 to
`mailto:Maya Carter <maya@pixelpress.example>` (or several addresses
separated by commas, for a group). The text is in memory any program may
read (global, under MiNT's memory protection) and stays there. EMail
opens a new message to them; as in any `mailto:` link, `?subject=`,
`cc=` and `body=` fill in the rest.

## Invitations in ECal

ECal keeps an event's invitees in its `ATTENDEE` lines with this user as
`ORGANIZER`, as RFC 5545 has them; a CalDAV server that schedules
(RFC 6638: iCloud, Fastmail, Nextcloud...) sends the invitations by
email and collects the answers. ECal writes them only when the invitees
were changed in its editor; otherwise the lines are kept exactly as they
came, so an event from an iPhone keeps everyone's answers.
