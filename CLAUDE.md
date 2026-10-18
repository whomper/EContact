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
  `git tag -a`); it tests, builds, tags vX.Y and publishes each file and the zip as a GitHub Release.
  A normal build must leave every tracked file unchanged and shows the version in `VERSION` as it is
  (there is no `-dev` label). Don't commit built programs, help files or certificates (`binary/`,
  `dist/`). Never reuse a number.
- **Only formal releases leave the build machine.** Erez gets vX.Y releases made by the Release workflow from `main`, nothing else: no test builds, no CI artifacts, no builds from a branch. Work lands on `main`; to try a fix on real hardware, release it. If a release doesn't work, roll back with a release: revert on `main`, release the next number, and say in the bad release's notes that it is withdrawn and which one to use.
- **Testing with another AERA program uses its latest release**, what Erez runs, never a build of its
  `main`: `common/tools/getrelease.py APP DIR FILE...` fetches it. Only for a change both programs are
  still making, test against the other's `main` at a named commit, say so, and test again against its
  release when it is out.
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

## Talking with Erez

- No progress or in-between messages: write to him only when he has to
  decide something, with a short context and the decision.
- Analysis or findings: the information, your assessment, and what he
  should do. Nothing more. (Messages to the hub stay complete, with the
  evidence.)

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
- EMail and ECal ask EContact about people with PEOPLE (the guidelines,
  4.1; docs/INTEGRATION.md): ui/answer.c answers names: and pick: from
  the contacts in memory (no file read, no alert, no mouse form) and
  sends changed: when people_names_stamp moves and when the background
  load ends; the window's choose mode is main.c's pick_start/pick_end.
  The accessory hands PEOPLE on to a running ECONTACT.PRG (people_pass).
  No program reads another's files: there is no CONTACTS.TXT. `make
  itest` checks the answers through card-cli ask.
- Where the accessory and the program find ECONTACT.INF and the folder
  follows ECal (ui/main.c: find_workdir, move_inf, locate_folder);
  `tests/hatari/placement.py WORK [MACHINE]` checks it.
- The accessory's memory outlives its window: when ECONTACT.INF, the key
  or a book's cache changed while it was closed (store_stamp), it lets
  everything go (store_release) and reads it all again (ui/main.c
  files_check). Anything new kept between openings must be let go there
  too. `make itest` (card-cli accessory) and
  `tests/hatari/acc_reread.py WORK [MACHINE]` check it.
- `binary/ECONTACT.HYP` (the Help key's ST-Guide guide) is made from
  docs/GUIDE.md by `make` when `hcp` is installed, and `make test` and
  `make dist` fail when it doesn't say the program's version:
  `common/tools/build-hcp.sh DIR` builds hcp from github.com/th-otto/hypview
  (the release workflow does the same); put it on the PATH or pass
  `HCP=`. Its first page is the family's front page (the guidelines,
  4.3): common/tools/helpfront.py makes it from docs/front.json, with
  docs/help/WINDOW.IMG (made once with `helpfront.py thumb` from
  docs/window.png). `tests/hatari/help_key.py` checks the Help key.
- The UI is checked in Hatari. The rigs take a machine of the family's
  (`ST`, `ST-1`, `TT`, `Falcon`, a screen after a colon: common/tools/machines.py,
  the guidelines, The machines) and get Hatari's arguments, the TOS and the
  network from it (tests/hatari/rig.py), never their own options.
  `tests/hatari/docs_shots.py WORK` takes the screenshots in docs/ (on
  Falcon:16 and ST:mono); with `--email` and `--ecal` it fetches EMail's
  and ECal's latest releases (getrelease.py). The rigs' stand-ins (fakeguide for ST-Guide, ASKER.PRG
  for EMail) are EContact's own test programs; `quiet_sync.py` uses
  common's FAKESTNG and serial bridge (common/tools/fakesting).
- Control shortcuts test `key_ctrl(kstate, key)` (common/atari/gem.h, the
  guidelines, 3.2), never `kstate & K_CTRL` alone: a key typed while
  EContact is busy comes with Control already let go.
  `tests/hatari/busy_keys.py` checks it. (Ctrl+Return in a multi-line
  field still needs K_CTRL: Return's code says nothing.)
- Falcon mode's curves go to the DSP (common/ui/dsp_ec.c, common/src/ecdsp.c,
  common/atari/dspec.c: the TLS editions only; the ST edition links
  ui/dsp_ec_none.c): falcon_dsp_ec_init() follows every
  falcon_dsp_init(). tls_sigs_path is SIGS.DAT in the folder,
  tls_file_stamp is pf_stamp, and PF_UPDATE (plat.h) writes SEED.DAT and
  ROOTS.DAT in place. `tests/hatari/falcon_tls.py WORK [MACHINE]` checks
  it against an ECDSA server.
- Text in the program is Atari text (Hebrew at 0xC2-0xDC, logical order);
  vCards on the wire are UTF-8.
- `third_party/tjpgd` is ChaN's TJpgDec with two changes for the 68000
  (see its README.txt): keep them if it is ever updated. Code that runs on
  an ST must not let GCC merge byte reads into word reads at odd addresses
  (an address error on a 68000): every 68000 build has -fno-store-merging
  and -fdisable-tree-bswap (GCC 13's bswap pass, the guidelines, The
  machines; not -fno-expensive-optimizations, which GCC 13.3 miscompiles
  with), and `-fdump-tree-bswap-details` must leave no "load in target
  endianness". GCC notes each file's disabled pass on stderr: not a warning. 32-bit `*` and `/` are software routines there: keep them out of
  per-pixel loops (photo.c, draw.c's draw_photo). draw_photo finds a
  colour's nearest pen the first time a photo has it (ph_pen: the whole
  table took 18 s in 256 colours) and, with more than 16 pens, draws each
  pen from its own list of pixels rather than scanning the photo once a
  pen; a change there must leave the photos as they were, pixel for pixel. src/jpegdc.c reads two
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
- While a sync runs (pf_keep, src/pfkeep.c) the files it opens again and
  again stay open: every Fopen takes an entry in TOS's small internal
  pool, and Erez's Falcon ran out of it ("OUT OF INTERNAL MEMORY") during
  a long Sync Contact Photos. TOS writes a file's length only when it is
  closed, so code must ask for sizes and dates, remove and rename through
  pf_* (they close a kept file first), never through GEMDOS directly.
- No file open across an event call in the accessory (the guidelines,
  2.2): its files belong to the program in front, whose end closes them
  and gives their numbers to the next files opened. pf_open's handles
  are pfkeep.c's own (FH + an entry); ui/evwrap.c, linked with
  `--wrap` (the Makefile's EVWRAP), calls pf_park before every
  evnt_multi, evnt_timer, form_do, form_alert and fsel_exinput, which
  closes every file, and each is opened again where it was at its next
  read, write or seek. Read and write files only through pf_*. The log
  says how many files a sync opened; `tests/hatari/acc_files.py WORK
  [MACHINE]` checks it all from Hatari's GEMDOS and AES trace.
- The accessory loads GDOS fonts only before its first event call, and
  only when FONT_ID= names one (font_boot; the guidelines, 2.2): their
  memory is the program in front's after that. Its Font... lists the
  fonts ECONTACT.FNT knows (with their ids), and one chosen is used from
  the next start. FONT_ID= is near the INF's start, where inf_value
  reads it at boot.
- Memory on an ST is the limit (README "How many contacts"). Reading a
  card leaves nothing in memory but its CONTACT (vcard_parse keeps its
  scratch from card to card): small mallocs left between contacts make
  every later malloc and free on the Atari slower. `BOOKnnnn.IDX` keeps
  parsed contacts (vcard_pack); a change to CONTACT's layout changes
  VC_PACK_ID, and a change to the parser's sources reparses (the
  Makefile's PARSE_SRC, summed into parse_id): add a file there when
  vcard_parse's result comes to depend on it. A new version alone keeps
  the index: reading every card again takes 13 s for 600 on a Falcon.
  Contacts that don't fit are counted (contacts_lost) and the user is
  told, never left out quietly.
- The index also names the copy of BOOKnnnn.DAT it was made from (the
  number on the DAT's first line, or its size and date): then nothing is
  hashed and the cards are found by where they are. Anything that writes
  the DAT other than by adding to its end must give it a new number
  (cw_open does).
- The accessory's quiet start (the guidelines, 2.2): before its first
  event call it only finds ECONTACT.INF (FOLDER=, START=) and takes its
  memory. Reading the books (abook.c books_load_*), the sort, the names stamp
  and the refresh with the window closed (quiet_sync_*) run in steps
  from ui/main.c bg_step through common/ui/bgwork.c, and no alert, busy
  bee or drawing happens until the window opens. A step must stay short
  on a 68000 (a card parsed is about 70 ms, four records hashed 35 ms, a
  24 KB memcpy 30 ms): new work goes in steps too, never a loop over all
  the contacts at once. A file is opened and closed within a step (the
  reader opens it again where it was): an accessory's handles belong to
  the program in front, which may end while we wait, so the background
  refresh keeps what came in memory and writes it with no event call
  between open and close. `tests/hatari/quiet_start.py` and
  `quiet_sync.py` check it with an AES_PROFILE build (README, Tests).
