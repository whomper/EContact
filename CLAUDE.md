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
- The UI is checked in Hatari: `tests/hatari/docs_shots.py WORK etos512us.img`
  takes the screenshots in docs/ (EmuTOS 512k, sourceforge.net/projects/emutos).
- Text in the program is Atari text (Hebrew at 0xC2-0xDC, logical order);
  vCards on the wire are UTF-8.
- `third_party/tjpgd` is ChaN's TJpgDec with two changes for the 68000
  (see its README.txt): keep them if it is ever updated. Code that runs on
  an ST must not let GCC merge byte reads into word reads at odd addresses,
  and 32-bit `*` and `/` are software routines there: keep them out of
  per-pixel loops (photo.c, draw.c's draw_photo).
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
