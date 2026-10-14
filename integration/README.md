# EMail and ECal with EContact

These patches make EContact the address book of
[EMail](https://github.com/whomper/EMAIL) and
[ECal](https://github.com/whomper/ECal). Each applies to the repository's
`main` (EMail at c58fe3f, ECal at 1488a2c) and builds and passes its tests:

```
cd EMAIL && git apply ../EContact/integration/EMAIL.patch && make && make test
cd ECal  && git apply ../EContact/integration/ECAL.patch  && make && make test
```

Both copy [`share/people.c`](../share/people.c) into their `src/`; when it
changes here, copy it again.

## EMAIL.patch

- **^B** (Message > Address book...) in a new message lists EContact's
  people and groups first, then the addresses EMail remembers in
  ADDRESS.TXT (those EContact has already are left out). A name begun on
  the To:, Cc: or Bcc: line ("Bob <b@x>, gr") narrows the list to what
  matches it and is replaced by the choice; with one match there's no
  list at all. A group puts all its members on the line.
- EContact's **mail** button opens a new message: EMail takes `VA_START`
  with `mailto:...` (see [docs/INTEGRATION.md](../docs/INTEGRATION.md)).
- **File > Contacts...** opens EContact's window.

Files: `ui/dialogs.c` (the list), `ui/editor.c` (completing the line),
`ui/main.c` (VA_START, the menu item, the boot drive), `src/people.c`,
`src/people.h`, `Makefile`. (EMail has `appl_find` and `appl_write` of
its own now.)

## ECAL.patch

- An event has **Invitees** under Location: addresses typed as
  "Name <a@b>, c@d", or **Add...** to pick people or a whole group from
  EContact, completing a name begun on the line.
- The invitees are written as `ATTENDEE` lines with the account's Apple ID
  (or email user name) as `ORGANIZER`; iCloud and other scheduling CalDAV
  servers send the invitations. Only a change to the invitees rewrites
  them: otherwise the event's attendees and their answers stay as they
  were.
- An invitation from someone else shows **From:** and who sent it,
  instead of the invitees.
- The menu has **Contacts...** to open EContact's window.

Files: `src/ics.c`, `src/ics.h` (attendees and organizer read and
written, `ics_each_address`, `ev_invited`), `src/calstore.c` (the
organizer set when saving), `ui/editor.c` (the Invitees line and
Add...), `ui/main.c` (the menu item), `atari/gem.c`, `src/people.c`,
`src/people.h`, `tests/unit.c` (17 more checks), `Makefile`.
