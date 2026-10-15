# Notes for working on EContact

EContact is one of the AERA Atari apps (ECal, EContact, EMail, EFTP, EHypView, ClaudeST). They are meant to
look and behave as one family.

## For every AERA app

These come from AERA-Atari's repo template and are the same in every app's CLAUDE.md.

- **Follow the AERA Atari app guidelines** ([ATARI-APP-GUIDELINES.md](https://github.com/whomper/AERA-Atari/blob/main/standards/ATARI-APP-GUIDELINES.md)). Pay most
  attention to:
  - About box
  - family shortcuts
  - PRG/ACC rules
  - context menus
  - VA_START, help, alerts
  - INF location

  When a change touches one of these, do it the guideline's way, even if the old code here did it
  differently.
- **Shared code lives in [AERA-Atari](https://github.com/whomper/AERA-Atari).** Files under `common/` come from it. Change them
  there, not here, and pull them in with `tools/sync-common.sh`. A fix made only in this repo will drift
  again.
- **Build and test before every push:** `make` with no warnings and `make test`, plus this repo's
  own checks (below): `make itest` when network code changed, the Hatari rigs in `tests/hatari/` when
  the GEM side changed.
- **Versions and releases:** `VERSION` changes only in a release commit. A release: `tools/bump.py`,
  commit, push, then run the Release workflow on `main` with the notes (or push a tag `vX.Y` made with
  `git tag -a`); it tests, builds with `RELEASE=1`, tags vX.Y and publishes each file and the zip as a
  GitHub Release. A normal build must leave every tracked file unchanged (it shows `x.y-dev`). Don't
  commit built programs, help files or certificates (`binary/`, `dist/`). Never reuse a number.
- **Every new source file starts with the SPDX line and copyright** (see any `.c` file); a generator
  writes them into the files it makes.
- **The name** reads "AERA | Erez Yaary" wherever it is given: copyright lines
  (`Copyright (C) 2026 AERA | Erez Yaary`), the About box, the help file's `@author`, the docs, and as
  the author of every commit: `git commit --author="AERA | Erez Yaary <yaary@me.com>"`, with Claude as
  the signing committer, so GitHub shows the commits as Verified (AERA-Atari,
  `standards/GITHUB-IDENTITY.md`).
- **Never commit:**
  - real addresses from the owner's network: use `192.168.1.x`, `PI_IP`, `ATARI_IP`
  - passwords, session keys, `.KEY` / `.INF` files from a test run
  - photos that still carry EXIF/GPS data
- **README:** keep the section order from the template (Highlights, Getting started, How it reaches the
  servers, Building, Source, Tests, Licence, family footer). Use British English.

## EContact

- **A release** carries `ECONTACT.ACC`, `ECONTACT.PRG`, `ECONTST.ACC`, `ECONTACT.HYP` and `.REF`,
  `CACERT.PEM`, `ROOTS.DAT` and the icon. The icons are committed in `icons/` (`make icons`).
- Before pushing: `make test` (the core) and `make itest` (card-cli against
  Radicale: `pip install radicale`), and `make` with no warnings.
- `gateway/` is a read-only copy of
  [AERA-Gateway](https://github.com/whomper/AERA-Gateway)'s: ecalgate (the
  `dav` service) serves EContact as it serves ECal. Change it there, with
  that repository's owner session, and bring it here with
  `tools/sync-gateway.sh`; `GATEWAY=gateway/ecalgate.py make itest` tests
  through it.
- `share/people.c` is also compiled into EMail and ECal (`src/people.c`
  there); `integration/*.patch` are the changes to those two. When either
  changes, regenerate the patch from a checkout of that repository's `main`
  (`git diff -- . ':!VERSION'`) and check it applies and builds.
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
  docs/GUIDE.md by `make` when `hcp` is installed, and `make test` and
  `make dist` fail when it doesn't say the program's version:
  `common/tools/build-hcp.sh DIR` builds hcp from github.com/th-otto/hypview
  (the release workflow does the same); put it on the PATH or pass
  `HCP=`. `tests/hatari/help_key.py` checks the Help key.
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
