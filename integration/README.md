# EMail and ECal with EContact

This patch makes EContact the address book of
[EMail](https://github.com/whomper/EMail). It applies to EMail's `main`
(1.33 at 9b64626) and builds and passes its tests:

```
cd EMAIL && git apply ../EContact/integration/EMAIL.patch && make && make test
```

[ECal](https://github.com/whomper/ECal) needs no patch any more: ECal 2.5
has the invitees, EContact's people (`src/people.c`, copied from
`share/people.c` here) and **Contacts...** itself. ECAL.patch was retired
then. ECal does two things differently from the patch: it keeps the
original `ATTENDEE` lines (and their answers) of the people still invited,
and it reads the attendees only of the event being edited.

Both copy [`share/people.c`](../share/people.c) into their `src/`; when it
changes here, copy it again.

## EMAIL.patch

- **^B** (Message > Address book...) in a new message lists EContact's
  people and groups first, then the addresses EMail remembers in
  ADDRESS.TXT (those EContact has already are left out). A name begun on
  the To:, Cc: or Bcc: line ("Bob <b@x>, gr") narrows the list to what
  matches it and is replaced by the choice; with one match there's no
  list at all. A group puts all its members on the line.
- **File > Contacts...** opens EContact's window.

Files: `ui/dialogs.c` (the list), `ui/editor.c` (completing the line),
`ui/main.c` (the menu item, the boot drive), `src/people.c`,
`src/people.h`, `Makefile`. EContact's **mail** button needs no patch:
EMail takes `VA_START` with `mailto:...` itself (see
[docs/INTEGRATION.md](../docs/INTEGRATION.md)).
