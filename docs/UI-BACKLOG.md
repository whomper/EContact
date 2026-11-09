# EContact's UI backlog (Erez's decisions of 9 Nov 2026)

From AERA-Atari a261597 (`standards/ui/DECISIONS-2026-11-09.md`, MENUS.md, CONTROLS.md, WINDOWS.md,
DEVIATIONS.md), as the hub listed it for EContact on 9 Nov 2026. EContact is in maintenance mode: nothing
here starts on its own. It rides a release EContact makes anyway (a fault fix, or one Erez asks for)
without growing that release's risk: the S items freely, the H and M items only when that release is
otherwise untouched. Every GEM release still passes the speed rig. When an item is done, send the hub one
line with the commit, and mark it here.

S small, H half a day, M more. [hub] waits for shared code in `common/`; the hub says when it is in.

1. `Direct connection` without `(TLS)` (D9, MD6). S.
2. `Add account...` moves to Options, before Settings (D13, MD27).
3. Pop-ups in sentence case with the bar's words: `Edit contact...`, `Delete...`, not `Edit Card`
   (D14, MD19; list.c, sidebar.c, card.c). S.
4. The toolbar's ≡ menu: About first, `EContact help  Help` and `Keys...` as the bar (MG16, MD16). S.
5. menu_update remembers the last state and asks the AES only for changes (8 calls after every event
   now; MD23). S.
6. Choose mode: Esc empties the search field and never cancels; Cancel or Undo end it (D28, C15).
7. The progress box says `Stop`, not `Cancel` (D31, W3); times as `2 min left`, sizes as
   `12 KB of 3.4 MB` (D35, W4). S.
8. A clipboard failure on the status line, `Couldn't write the clipboard.`, not an alert (D50, W17). S.
9. The notification's button `Show`, not `Open` (D51, W25). S.
10. The window: one minimum (the open check's 50 x 14 against WM_SIZED's 44 x 12; D38, W10); the first
    run on the whole desk, not inset (D39, W11); Shift+Up/Down a page of rows - 1 (W6) [hub: key_shift].
    S each.
11. `sel_color` reads pens 0 to 15 (D42, W16); the palette stays. S.
12. A pop-up opens with its ticked choice over its button (D27, C8) [hub]. S.
13. Insert opens the context menu (D17, MD20); where it opens is still open. H.
14. Tick boxes become bitmap ticks (D23, C5). H.
15. Buttons act on the release over the button (C7); buttons only. H.
16. The progress bar drawn with `progbar` (C22). H.
17. The INF's `WINDOW=x,y,w,h` and `FONT=name,points`, the old keys read once (D43, D44, W18);
    `SIDEBAR=` stays. M.
18. The older dialogs to the AES's look: rows, focus ring, pop-up (C24). M.
19. Tooltips on the toolbar, 0.7 s (D18) [hub], H; keyboard reach (D25) [hub].

Still open (do not start): Find's words (D4); press or release for ticks and pop-ups.
Nothing to do: Select all stays in Contact (D20); Quit alone already (D3); copying never takes a password.
