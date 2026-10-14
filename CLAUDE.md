# Notes for working on EContact

- **Every new build gets a new version.** `make` counts the minor number in
  `VERSION` up whenever anything was compiled again (`tools/bump.py`), as in
  ECal and EMail: commit `VERSION` with `binary/`, and never reuse a number.
- Before pushing: `make test` (the core) and `make itest` (card-cli against
  Radicale: `pip install radicale`), and `make` with no warnings.
- `share/people.c` is also compiled into EMail and ECal (`src/people.c`
  there); `integration/*.patch` are the changes to those two. When either
  changes, regenerate the patch from a checkout of that repository's `main`
  (`git diff -- . ':!VERSION' ':!binary'`) and check it applies and builds.
- Where the accessory and the program find ECONTACT.INF and the folder
  follows ECal (ui/main.c: find_workdir, move_inf, locate_folder);
  `tests/hatari/placement.py WORK etos512us.img` checks it.
- The accessory's memory outlives its window: when ECONTACT.INF, the key
  or a book's cache changed while it was closed (store_stamp), it lets
  everything go (store_release) and reads it all again (ui/main.c
  files_check). Anything new kept between openings must be let go there
  too. `make itest` (card-cli accessory) and
  `tests/hatari/acc_reread.py WORK etos512us.img` check it.
- `binary/ECONTACT.HYP` (the Help key's ST-Guide guide) is made from
  docs/GUIDE.md by `make` when `hcp` is installed, and `make test` fails
  when it doesn't say the program's version: build hcp from
  github.com/th-otto/hypview (`./autogen.sh && ./configure && make -C hyp
  hcp`; if linking fails on include/linux/libc.so, link the objects by
  hand without it) and put it on the PATH. `tests/hatari/help_key.py`
  checks the Help key.
- The UI is checked in Hatari: `tests/hatari/docs_shots.py WORK etos512us.img`
  takes the screenshots in docs/ (EmuTOS 512k, sourceforge.net/projects/emutos).
- Text in the program is Atari text (Hebrew at 0xC2-0xDC, logical order);
  vCards on the wire are UTF-8.
- `third_party/tjpgd` is ChaN's TJpgDec with two changes for the 68000
  (see its README.txt): keep them if it is ever updated. Code that runs on
  an ST must not let GCC merge byte reads into word reads at odd addresses,
  and 32-bit `*` and `/` are software routines there: keep them out of
  per-pixel loops (photo.c, draw.c's draw_photo). src/jpegdc.c reads two
  bytes through a volatile pointer for the same reason (rd16): check the
  sample formats in Hatari on an ST after touching it.
- Photo failures say why (photo_why, PW_...): the status line, the alert
  after Sync Contact Photos, a line each in the log. A failure is kept in
  the .PIC with the decoder's number (PHOTO_DECODER): raise it when the
  decoders read more, so old failures are tried again.
- The photo cache's rules (photo.h) are tested by `make itest`: a change to
  them needs the tests to say so.
- Syncing fetches cards without their photos (abook.c, CardDAV partial
  retrieval); `tests/host/partialproxy.py` plays a server that does it,
  since Radicale doesn't. A card kept in part (RESOURCE.partial) must be
  fetched whole before anything writes it back: go through contact_text(),
  never contact_raw(), when changing a card. The cache's photo stand-in
  (`X-ECONTACT-PHOTO`) must never reach a server or a file.
- Anything that reads the list again frees the CONTACTs (people_serial
  counts up): fetching a card whole for its photo does, so the UI calls
  after_change() and finds the chosen one again by UID.
- Memory on an ST is the limit (README "How many contacts"). Reading a
  card leaves nothing in memory but its CONTACT (vcard_parse keeps its
  scratch from card to card): small mallocs left between contacts make
  every later malloc and free on the Atari slower. `BOOKnnnn.IDX` keeps
  parsed contacts (vcard_pack); a change to CONTACT's layout changes
  VC_PACK_ID, and a new version reparses anyway. Contacts that don't fit
  are counted (contacts_lost) and the user is told, never left out quietly.
- `common/` is shared AERA code (BearSSL, the DSP RSA, bidi, dates,
  passwords, libc headers, build tools), a git subtree of
  github.com/whomper/AERA-Atari. Don't edit it here: change it in
  AERA-Atari, then run `tools/sync-common.sh`. Follow the AERA Atari app
  guidelines (AERA-Atari, `standards/ATARI-APP-GUIDELINES.md`).
