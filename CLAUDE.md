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
