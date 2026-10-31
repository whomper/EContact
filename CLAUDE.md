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
- **Speed is set by the stock machines** (Erez, 26 Oct 2026; the guidelines' "Speed"): an 8 MHz ST
  and a stock Falcon (`ST`, `ST-1`, `Falcon-4`), not the DFB1. Design and code for them: draw only
  what changed, keep the 68000's slow operations out of inner loops, do work once. A change to
  drawing, a loop over the data or the start gives its `AESGAP` stretches on `ST` and `Falcon-4`,
  before and after, in its commit message. A GEM release passes the speed rig within the
  guidelines' times.
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
- **Releases go out without asking Erez** (his decision, 23 Oct 2026; guidelines 1.2). Release when
  work on `main` is ready:
  - this repo's checks pass;
  - the rigs on his TOS pass, where this file asks for them;
  - the hub's family checks pass.

  Then give Erez the link in one line, with anything he has to do, and send the hub the link. Roll
  back a bad release the same way. Decisions that are his still go to him first.
- **Who decides what** (Erez, 31 Oct 2026; the guidelines' "Who decides what"). Erez's part, put
  to him before the work as a decision with a short context:
  - new programs and ideas, their core capabilities, and how they work with the other programs;
  - the user experience: the look, the layout, the way through a task, the words, the icons, the
    defaults;
  - phases: work stays inside the phase he approved; at its end, one phase report (what it set out
    to do, what was done with the release links, what is left, the next phase as a proposal), and
    the next phase starts when he says so;
  - priorities and conflicts between programs or sessions (through the hub, with its options and
    recommendation);
  - packages on the Pi and ports.

  How it is made is the team's, without asking: code, structure, shared code, tests, measurements,
  fixes, docs and releases inside an approved phase.
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

- **A release** carries `ECONTACT.ACC`, `ECONTACT.PRG`, `TLS.OVL`, `ECONTACT.HYP` and `.REF`,
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
  `make itest` checks the answers through card-cli ask.
- CONTACTS.TXT in the folder is for EMail and ECal when EContact isn't
  loaded, an option of theirs (the guidelines 4.1; Erez, 21 Oct 2026; no
  setting here): abook.c people_file_step writes it in 1.40's format (P
  and G lines, "; made from" a stamp) in steps, run by bg_step (the
  accessory) and the event loop (the program, the window open). It is
  looked at whenever groups_collect runs; its stamp is the names stamp,
  the group cards' members and the categories, so a start that changed
  nothing writes nothing (`make itest`, `tests/hatari/quiet_start.py`).
  With contacts left out for lack of memory it isn't written. Its lines
  are put together by hand (an ST's snprintf takes 2.9 ms a line, strpbrk
  1.3 ms an address) and its steps stop after some 20 ms. It is made
  8 KB at a time (PF_CHUNK), each piece added to CONTACTS.TMP in a step
  of its own, which is then renamed: the whole file (40 KB for 350
  contacts) wasn't to be had on a 1 MB ST, nor 10 KB at its first boot
  (a quarter of the heap): a piece is half as big while the heap hasn't
  its room, down to 1 KB (PF_LEAST; `quiet_start.py WORK ST-1`). The
  stamp's steps go by the piece's size too: pfw.chunk is set where the
  stamp starts.
  A load collects the groups in steps too
  (groups_collect_step: 600 contacts with categories took an ST 165 ms).
- Where the accessory and the program find ECONTACT.INF and the folder
  follows ECal (ui/main.c: find_workdir, move_inf, locate_folder);
  `tests/hatari/placement.py WORK [MACHINE]` checks it. The program's
  folder is common's app_dir() (its shel_read command), never the
  current folder (the guidelines 6.1, 28 Oct 2026): TOS 1.04 and 4.04
  start a program another one starts with shel_write in the desktop's
  folder. placement.py's fourth case starts it from a floppy with
  common/tests/appdir's LAUNCH.PRG (a Falcon as Falcon-4 on a mono
  monitor: on VGA Hatari starts it in ST low with a floppy in, and the
  floppy is found damaged). The accessory keeps FOLDER=.
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
- The speed rig is `tests/hatari/speed.py WORK [MACHINE...] [--nvdi] [--profile ELF]` (README,
  Tests): an AES_PROFILE=20 build, 621 contacts, each action's longest stretch set against the
  guidelines' time. The profile build's ui/main.c says when the start and the accessory's opening
  have drawn the window (AESDRAWN) and has prof_turn_end, where --profile stops Hatari to save its
  profiler's counts (hatari_profile -p reads them); -DWIN_DIRECT draws straight on the screen, to
  measure the off-screen buffer against it.
- The UI is checked in Hatari. The rigs take a machine of the family's
  (`ST`, `ST-1`, `TT`, `Falcon`, a screen after a colon: common/tools/machines.py,
  the guidelines, The machines) and get Hatari's arguments, the TOS and the
  network from it (tests/hatari/rig.py), never their own options.
  `tests/hatari/docs_shots.py WORK` takes the screenshots in docs/ (on
  Falcon:256, every app picture in 256 colours: Erez, 28 Oct 2026; and
  st-mono.png on ST:mono); with `--email` and `--ecal` it fetches EMail's
  and ECal's latest releases (getrelease.py). The rigs' stand-ins (fakeguide for ST-Guide, ASKER.PRG
  for EMail) are EContact's own test programs; `quiet_sync.py` uses
  common's FAKESTNG and serial bridge (common/tools/fakesting).
- Control shortcuts test `key_ctrl(kstate, key)` (common/atari/gem.h, the
  guidelines, 3.2), never `kstate & K_CTRL` alone: a key typed while
  EContact is busy comes with Control already let go.
  `tests/hatari/busy_keys.py` checks it. (Ctrl+Return in a multi-line
  field still needs K_CTRL: Return's code says nothing.)
- One program for every Atari (the guidelines 7.4): the TLS (tls.c,
  BearSSL, the DSP's RSA and curves, 68030 code) is in TLS.OVL, common's
  one file (common/tlsovl), copied to binary/ and shipped, never built
  here. The program links common's atari/tls_ovl.c, src/ovl.c and
  ui/dsp_ovl.c and only BearSSL's MD5 (Digest logins, 68000 code); the
  host builds link src/tls.c, which answers the same calls. TLS.OVL is
  loaded on a 68020 or better the first time a Direct connection needs
  it, and when Direct connection is ticked (falcon_mode_set:
  tls_ovl_load's error in an alert, the box left unticked), into the
  program's memory: the accessory's heap holds 150 KB more for it when
  FALCON= is on (main.c OVL_ROOM, and mem_note until it is loaded), and
  store_release lets it go with no connection open. tls_ovl_path is
  TLS.OVL in the folder, beside CACERT.PEM. The user sees "Direct
  connection", never "Falcon mode"; the INF keeps FALCON=. The AV and
  twin names are ECONTACT and EMail's EMAIL alone (the ST editions'
  names went with ECal 2.25, the hub). falcon_dsp_ec_init()
  follows every falcon_dsp_init(). tls_sigs_path is SIGS.DAT in the folder,
  tls_file_stamp is pf_stamp, and PF_UPDATE (plat.h) writes SEED.DAT and
  ROOTS.DAT in place. `tests/hatari/falcon_tls.py WORK [MACHINE]` checks
  it against an ECDSA server, and a run without TLS.OVL.
- The window is drawn off the screen and copied at once (ui/win.c, ECal's
  way: an EdDI bitmap under NVDI, else TOS's logical screen moved to a
  buffer while drawing; a test at start-up first, ECONTACT.LOG says
  which). The accessory takes the screen-sized buffer before its first
  event call, only when it is a sixteenth of what was free at most (not
  on a 1 MB ST). Scrolling the list moves the rows on the screen
  (win_scroll) and draws those that come in and the slider's old and
  new place only, and nothing of it when the slider stays in its pixels
  (the whole bar took an 8 MHz ST 70 ms, its slider's part 50); a held arrow
  keeps some 15 rows a second, a held track goes on to the pointer.
  list_draw draws only the rows in its area, and choosing someone
  (select_contact) draws only the old and the new chosen rows, the rows
  that stay moved when the list scrolls to it, and the card later: bands
  of two text lines, some 40 ms a turn from the event loop, into the
  buffer, copied when it is all there (win_redraw_later; 0.47 s of an
  ST's time in one piece with the rows). A band that cuts the round
  picture takes all of it (card_avatar_area: the VDI's filled circle
  costs 60 ms whatever part shows), and the picture, the buttons and
  text_at's text (the system font's cells) draw nothing outside the clip:
  a band's own cost is what keeps it short. The screen keeps the old card
  meanwhile, another choice starts it again (a held Down only moves the
  bar), and a scroll, a move or a whole redraw finishes it first; the
  photo waits for it. A search draws the card (later) only when another
  one is chosen. sel_contact() is kept until another is chosen or
  people_serial moves (its uid against 621 others: 12 ms on an ST). The buffer holds the
  whole window as it was last drawn (obuf_whole): what the AES asks for
  again (WM_REDRAW: a dialog, an alert or a window gone from over it) is
  copied from it (win_uncover), a scroll moves it too, a move at the
  same size moves it (buffer_moved), a new layout or the window closed
  forget it. So everything in the window is drawn through win_redraw,
  never on the screen alone, or the next uncover shows the old picture;
  speed.py checks that the screen is what a whole redraw draws. On TOS
  1.04 (an 8 MHz ST, ST:mono) a v_gtext of 18 letters takes 2.9 ms at an
  x on a byte, 17 ms off it and 24 ms in bold (6 to 8 times as long;
  under NVDI each under 1 ms): measure before adding text to a row.
  So text starts on a byte (the guidelines' Speed: text on a byte): the
  work area on 16 pixels (snap_x, when opened, moved, sized and fulled),
  the panes' widths, the margins, the toolbar's buttons, the search
  field's text, the card's name column and the dialogs' boxes (field.c
  on_byte) on 8-pixel steps. Plain black text on a white ground
  (TX_WHITE: the list, the card, the sidebar, the dialogs' fields) goes
  through common's text_plain, straight into TOS's screen or the buffer
  (it reads Logbase), the VDI's pixels byte for byte; clip_on and
  clip_off set the clip through common's text_clip and text_noclip,
  which it reads, so ui/ never calls vs_clip itself. A name's right part
  is drawn before its left, whose bold is ORed a pixel onto it. The
  list's scroll stays the VDI's copy: common's scr_rows made the
  arrow's line 215 ms on an ST against 145 (a narrow pane, a 16-pixel
  group cut at each side of every row).
  Bold with TX_WHITE (the ground under it is pen 0: the list's rows and
  headings, the card's name, flat dialogs) is drawn plain and ORed onto
  itself a pixel to the right under TOS's own VDI (draw.c smear: what
  its bold is, in the system font; a third of the time), the VDI's bold
  elsewhere (NVDI, EdDI, another font, inverse, a grey ground). The
  screen must stay the same: compare screenshots of the old and new
  builds on ST:mono, ST:med, Falcon:16 and EmuTOS. The
  list's bar is the
  family's pane bar (the guidelines 5.1, common/ui/panebar.c: AES objects
  in the AES's look, flat, 3D or a theme's), never drawn by hand: AES
  objects draw on the screen, so win_redraw draws it (list_bar_draw) on
  each piece after the copy, and a scroll draws pb_moved's part
  (win_bars), with 3 pixels more each way within the bar: a raised
  slider's edge stands out of the track, over the up arrow's foot. The
  window resizes by the AES's sizer only.
  `tests/hatari/list_bar.py WORK [MACHINE] [--acc] [--nvdi]`
  checks it. The card, the pane at the window's right edge, scrolls in
  the window's own slider column (5.1: the AES keeps it anyway): card.c
  card_top, win_slider sets WF_VSLSIZE/WF_VSLIDE after each redraw,
  WM_ARROWED (the wheel over the list stays the list's) and WM_VSLID
  scroll it, one click a line or a page (5.1, "One click, one step": TOS
  4's AES sends each click twice, common's evnt_multi_ drops the echo);
  `tests/hatari/card_scroll.py WORK [MACHINE]` measures both.
- EContact's dialogs are its own (ui/field.c, not GEM's fields nor
  common/ui/field.c, which is for GEM object trees): a text field holds
  what its setting holds and shows a window of it, scrolled with the
  cursor (the guidelines 5.4); `max` is the buffer, `w` the width shown,
  and dlg_ask's len is what the buffer holds (it shows 30 at most).
  Shift+Left/Right go to the ends, known by their '4' and '6' too (as
  key_ctrl does: Shift may be let go before a busy EContact reads them).
  `tests/hatari/long_field.py` checks it. Esc empties the text field the
  cursor is in and the dialog stays open, Undo (and a dialog window's
  closer, common's dlg_evnt) cancels, Esc cancels a dialog without a
  text field (dlg_cancel_key; the guidelines 3.5, GEM's way since 27 Oct
  2026); a text of several lines never empties on Esc. The window's
  search field empties on Esc too, and in the choose mode Esc cancels
  once it is empty (`tests/hatari/esc_test.py`). The buttons are the
  AES's own G_BUTTONs, drawn by objc_draw in the clip set (field.c
  aes_button; 5.6), a dialog's together in one call (aes_buttons: TOS
  1.04 takes some 10 ms a call), and a pop-up opens over its button with
  its choice ticked (popup_over; 5.7). Where the AES draws 3D (the guidelines 5.6;
  dlg_look3d: no setting since 27 Oct 2026), in 16 colours or more,
  they take the AES's look: the ground its grey (pen 8), pop-ups raised
  (one black line), fields, ticks, lists and the progress bar white,
  the chosen section a black bar; anything cleared is cleared with the
  ground (bg), never white. Elsewhere they are flat, as before. Where the AES runs
  several programs (MiNT, MagiC: dlg_windowed; the guidelines 3.5) each
  one, the pick list and the progress box open in a window of the
  AES's (field.c box_open, common's dlg_open_area), less their own
  frame and title bar, which are the AES's: box follows the window
  (box_follow), all drawing goes through clip_dlg (dlg_cut), a piece
  of any dialog open is drawn when the AES asks (piece_at, the levels),
  the loops take events through evnt_dlg (dlg_evnt), the main window is
  drawn behind them (win_behind, dlg_redraw_hook; not during Sync
  Contact Photos), other messages are kept and sent back when the last
  one closes, and a menu title chosen meanwhile does nothing
  (menu_drop). On TOS and EmuTOS box_open does what it always did:
  form_dial and the screen held. `tests/hatari/xaaes_dialogs.py WORK`
  checks it under FreeMiNT and XaAES.
- Several contacts are chosen at once as in EFTP's and EMail's lists
  (Erez, 28 Oct 2026): Shift+click one more or fewer, Control+click a
  range from the one chosen (sel_uid, where a range starts), ^A (Select
  all) everyone shown. They carry MK_SEL in their mark (the pick's ticks
  are MK_TICK: CONTACT's layout and VC_PACK_ID stay), counted in
  list.c's nchosen, 0 while one alone is chosen; the marks go with the
  CONTACTs when the list is read again (people_serial) and the list
  made again forgets them. Only the rows whose bar changed are drawn
  (lit_take, lit_redraw), the card says "N Cards Selected". Delete,
  Add to Group, Remove from (group), Send Email, ^C and Export take them
  all (list.c chosen_menu, main.c delete_chosen, cmd_copy_chosen,
  cmd_mail_chosen); every change reads the list again, so a command
  takes their UIDs first (list_chosen_uids) and finds each again; a
  group card is written once with them all (abook.c group_add_uids,
  group_remove_uids: one PUT, not one a contact). `make itest` and
  `tests/hatari/multi_select.py WORK [MACHINE]` check it.
- Text in the program is Atari text (Hebrew at 0xC2-0xDC, logical order);
  vCards on the wire are UTF-8.
- `third_party/tjpgd` is ChaN's TJpgDec with two changes for the 68000
  (see its README.txt): keep them if it is ever updated. Code that runs on
  an ST must not let GCC merge byte reads into word reads at odd addresses
  (an address error on a 68000): every 68000 build has -fno-store-merging
  and -fdisable-tree-bswap (GCC 13's bswap pass, the guidelines, The
  machines; not -fno-expensive-optimizations, which GCC 13.3 miscompiles
  with), and `-fdump-tree-bswap-details` must leave no "load in target
  endianness". Nor a word or long at an odd stack offset (GCC fills an
  initialised char array on the stack with long moves wherever it lies:
  common's accmem, 27 Oct 2026): `make test` runs common/tools/oddaccess.py
  over the 68000 ELFs; such an array gets `__attribute__((aligned(4)))`
  or is filled by strcpy. GCC notes each file's disabled pass on stderr: not a warning. 32-bit `*` and `/` are software routines there: keep them out of
  per-pixel loops (photo.c, draw.c's draw_photo). draw_photo finds a
  colour's nearest pen the first time a photo has it (ph_pen: the whole
  table took 18 s in 256 colours) and, with more than 16 pens, draws each
  pen from its own list of pixels rather than scanning the photo once a
  pen; black and white (ST high and medium) is dithered in a loop of its
  own, one channel (the three-channel loop took an ST 720 ms a photo,
  this one 390). A photo got is dithered a few rows a turn (draw.c
  photo_dither, card.c photo_slice), the initials shown until it is
  ready; a redraw meanwhile finishes it at once. A change there must
  leave the photos as they were,
  pixel for pixel (compare screenshots of the old and new builds on
  ST:mono, ST:med and Falcon:16). src/jpegdc.c reads two
  bytes through a volatile pointer for the same reason (rd16): check the
  sample formats in Hatari on an ST after touching it.
- The search (contact_matches, src/abook.c) makes the text ready once
  (match_ready: its first letter in both cases, its digits) and compares
  only where that letter is; the name as the list shows it is made only
  for a text with a space. Typing on narrows what the last search found
  (list.c narrows), not after digits (a phone number matches without its
  dashes); anything else that makes the list again (list_invalidate)
  looks through everyone. A search's matching is a job, some 30 ms a turn
  from the event loop (list_search_step, win_search_step; 621 contacts
  took an ST 0.3 s): the field shows the text at once, the list stays as
  it was until the job is done (it is valid meanwhile), then it is drawn
  in bands with the card (win_redraw_later takes both, from the top), the
  card only when another is chosen; a key meanwhile starts it again. `make test` sets it against the old matcher
  over 17,000 cards and texts (tests/unit.c test_matches): it must find
  the same.
- Sync Contact Photos that ends with the window behind another
  program's says so as a BGEM.NG notification, its Open bringing the
  window to the front (main.c note_behind, common/ui/notify.c; the
  guidelines 4.5); where notify_send gives 0 (TOS, MagiC, no BGEM.NG) and
  with the window in front, the alert as before. notify_init and
  notify_ask (notify_send gives 0 until BGEM.NG said yes) run before the
  first event call. The About box's three status lines are the family's
  (ui/about.c: common's about_net_line and about_mem_line, and "N
  contacts in M address books"; the guidelines 1.3).
- Photo failures say why (photo_why, PW_...): the status line, the alert
  after Sync Contact Photos, a line each in the log. A failure is kept in
  the pack's entry with the decoder's number (PHOTO_DECODER): raise it when
  the decoders read more, so old failures are tried again.
- The pictures are in one file, CACHE\PHOTOS.PAK (src/photopack.c, the
  guidelines 6.5: many small items in one file with an index): records
  only ever added after the old index, then a new index and the header in
  one open, so a reset leaves the old index whole; the index is held in
  memory, so whether a contact has its picture costs no file. 1.49's
  CACHE\PHOTOS isn't read (Erez: the cache is deleted and made again).
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
- Disk access goes through memory (the guidelines, 6.5): a job (a
  refresh, Sync Contact Photos, the index written after a load) gathers
  what it writes in abook.c's OUT buffers and writes each file once, at its
  end or when ab_out_max is reached, never a write per card or photo; no
  file is open meanwhile, so pf_park has nothing to reopen. Sync Contact
  Photos fetches into book_fetch_job's memory, decodes from there
  (contact_raw reads the job's memory first) and writes at
  book_fetch_job_end, as many cards a request as ab_job_bytes holds by
  the last answer's size (carddav_card_bytes), written before the next
  round would overfill it (book_fetch_job_room). With under 1.5 MB free
  at boot (a 1 MB ST; a 4 MB Falcon with NVDI and ECal on a first boot)
  the accessory doesn't sync photos (ab_job_bytes 24 KB: cmd_sync_photos
  says so, with the memory free; Erez: the memory is the contacts'). Sizes and dates come from one look (pf_info);
  ECONTACT.INF is read once at boot (inf_read); reads are ab_read_bytes
  (16 KB on a 68000, 32 KB on a 68030). The job's log is held
  (conn_log_hold) and written at its end. `tests/hatari/acc_files.py`
  fails above each job's budget of opens.
- The accessory's memory is what it measured (mem_note): common's
  heap_peak since boot, at least what it holds plus room for a job and a
  photo shown, and a tenth more, written to ECONTACT.INF as MEMKB= and
  taken at the next boot (acc_heap through common/ui/accmem.c; HEAPKB=
  overrides; the first boot takes a quarter of the memory free, half
  below 1 MB). The log's memory: line is accmem's (acc_mem_line, with
  heap_early). Bulk memory has fixed sizes by the memory free at
  boot (job_memory), not a share of the heap.
- The accessory loads GDOS fonts only before its first event call
  (font_boot; the guidelines, 2.2): their memory is the program in
  front's after that. Under NVDI (its cookie) always, so its Font... is
  the program's and a font chosen is used at once (Erez: the accessory on
  par with the program); under FONTGDOS or SpeedoGDOS only when FONT_ID=
  names one. Without them its Font... lists the fonts ECONTACT.FNT knows
  (read at each opening, with their ids), and one chosen is used from
  the next start. The dialog is the family's, common/ui/fontsel.c (the
  guidelines 5.3): ui/font.c only fills it and takes the font (take);
  change the dialog in AERA-Atari. FONT_ID= is near the INF's start, where inf_value
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
- vcard.c's reading is written for the 68000, where each pass over a
  card's text costs some 10 us a letter: a line is read where it is
  (copied only when folded), made in its place in the array, its name
  known once (prop_of, P_...), a plain value put in the pool without
  cs_to_atari's malloc, a folded photo's lines passed over, no snprintf
  or strncat. A change there must leave every result as it was:
  compare the old and new parser's output over many cards (CONTACT, the
  card vcard_write makes of it, vcard_set_categories, the photo span),
  as 1.57's was, byte for byte over 8,000 inputs.
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
  on a 68000 (a card parsed is about 40 ms, four records hashed 35 ms, a
  24 KB memcpy 30 ms): new work goes in steps too, never a loop over all
  the contacts at once (ECONTACT.INF's lines too: store_init_lines, 30 a
  step; a card read from the disk is parsed in the next step). No slice
  runs in the turn a message came in: its work and a slice would make one
  stretch (ECal asks about people while the books are read). Nor in a
  turn that took 30 ms already (turn_free): on TOS 1.04 an AES call such
  as appl_find (bg_tick's look for ECONTACT.PRG, once a second) can let
  another program run, and ECal's 80 ms and a slice made one 160 ms
  stretch. A file is opened and closed within a step (the
  reader opens it again where it was): an accessory's handles belong to
  the program in front, which may end while we wait, so the background
  refresh keeps what came in memory and writes it with no event call
  between open and close. `tests/hatari/quiet_start.py` and
  `quiet_sync.py` check it with an AES_PROFILE build (README, Tests).
- The program reads its books the same way, after its window is drawn
  (main.c prg_loading, a slice a turn: in one stretch they were 9 s of an
  8 MHz ST's start). Meanwhile a menu entry, files dropped or given by
  VA_START and keys wait and come afterwards, as if typed then (^Q and ^U,
  the window's messages and PEOPLE at once), a click does nothing, and the
  list stays empty until they are all read and sorted (list.c rebuild);
  a .vcf given at the start reads what is left at once. Then the window
  is drawn a pane at a time, a text line of it a turn, each band copied
  when drawn and the list's bar at the end (win.c win_redraw_bands: 1.2 s
  of an ST's in one piece; a line across the window was 120 ms); a part
  asked for meanwhile is drawn again only if its pane was drawn already.
  The accessory's window, its books read, opens the same way
  (win_open_bands: 1.27 s of an ST's in one piece), the AES's WM_REDRAW
  meanwhile showing what is drawn and white for the rest (uncover_bands).
  A band's cost is mostly the band's (some 35 ms whatever its height on
  TOS 1.04): its height doubles while a band takes 60 ms at most, halves
  over 120, the toolbar is one band and the list's end on a row's edge
  (list_band); anything drawn skips what is outside the clip before
  making it (side_draw's rows). speed.py times the window drawn and the
  longest slice.
