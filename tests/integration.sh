#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# EContact's core against a real CardDAV server (Radicale), with card-cli
# (the same abook/CardDAV/HTTP/TLS code as the Atari):
#
#   tests/integration.sh             plain HTTP, straight to the server
#   GATEWAY=gateway/ecalgate.py tests/integration.sh
#                                    through the Pi gateway (or another copy of it)
#
# Needs: pip install radicale.
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=$PWD/build/card-cli
W=$(mktemp -d)
PIDS=()
cleanup() { for p in "${PIDS[@]:-}"; do kill "$p" 2>/dev/null || true; done; rm -rf "$W"; }
trap cleanup EXIT
PORT=$((20000 + RANDOM % 20000))
GWPORT=$((PORT + 2))
fails=0
ok()   { echo "  ok   $*"; }
bad()  { echo "  FAIL $*"; fails=$((fails + 1)); }
expect() { # expect "description" "needle" "haystack"
  if grep -qF -- "$2" <<<"$3"; then ok "$1"; else bad "$1: no '$2' in:"; echo "$3" | sed 's/^/       /'; fi
}
absent() {
  if grep -qF -- "$2" <<<"$3"; then bad "$1: '$2' still there"; else ok "$1"; fi
}

printf "alice:wonderland\nbob:builder\n" > "$W/users"
python3 -m radicale --server-hosts "127.0.0.1:$PORT" --storage-filesystem-folder "$W/store" \
  --auth-type htpasswd --auth-htpasswd-filename "$W/users" --auth-htpasswd-encryption plain \
  > "$W/radicale.log" 2>&1 &
PIDS+=($!)
BASE="http://localhost:$PORT"
CURL=(curl -s -u alice:wonderland)
for i in $(seq 50); do "${CURL[@]}" -o /dev/null "$BASE/" && break; sleep 0.2; done

# an address book with a Hebrew name, and one contact made elsewhere (an iPhone)
"${CURL[@]}" -X MKCOL "$BASE/alice/friends/" -H "Content-Type: application/xml" --data-binary @- >/dev/null <<XML
<?xml version="1.0" encoding="utf-8"?>
<D:mkcol xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:carddav"><D:set><D:prop>
<D:resourcetype><D:collection/><C:addressbook/></D:resourcetype><D:displayname>חברים</D:displayname>
</D:prop></D:set></D:mkcol>
XML
"${CURL[@]}" -X PUT "$BASE/alice/friends/dana.vcf" -H "Content-Type: text/vcard" --data-binary @- >/dev/null <<VCF
BEGIN:VCARD
VERSION:3.0
N:Levi;Dana;;;
FN:Dana Levi
EMAIL;TYPE=INTERNET;TYPE=WORK:dana@example.org
TEL;TYPE=CELL:+972 50 111 2222
item1.X-ABRELATEDNAMES:Avi
item1.X-ABLabel:_\$!<Spouse>!\$_
UID:DANA-1
END:VCARD
VCF

D=$W/atari
if [ -n "${GATEWAY:-}" ]; then
  python3 "$GATEWAY" --port "$GWPORT" --allow-local > "$W/gate.log" 2>&1 &
  PIDS+=($!)
  sleep 1
  $CLI "$D" gateway 127.0.0.1 "$GWPORT"
  echo "through the gateway"
fi

echo "account and first sync"
$CLI "$D" carddav Home "$BASE/" alice wonderland
R=$($CLI "$D" books)
expect "the address book found, Hebrew name" "חברים (Home) 1 contacts" "$R"
R=$($CLI "$D" list)
expect "the iPhone's contact" "Dana Levi | dana@example.org | +972 50 111 2222" "$R"

echo "add, change, keep what we don't edit"
$CLI "$D" add "חברים" Erez Yaary erez@example.com "+972 52 333 4444"
R=$($CLI "$D" list)
expect "added" "Erez Yaary | erez@example.com | +972 52 333 4444" "$R"
$CLI "$D" set "Dana Levi" title "Engineer"
$CLI "$D" set "Dana Levi" email "dana@home.example"
R=$("${CURL[@]}" "$BASE/alice/friends/dana.vcf")
expect "the title on the server" "TITLE:Engineer" "$R"
expect "the second email" "dana@home.example" "$R"
expect "the spouse kept" "item1.X-ABRELATEDNAMES:Avi" "$R"
if grep -qiF 'item1.X-ABLabel:_$!<Spouse>!$_' <<<"$R"; then ok "its label kept"; else bad "its label kept"; fi
expect "the UID kept" "UID:DANA-1" "$R"

echo "a change made elsewhere comes over"
"${CURL[@]}" -X PUT "$BASE/alice/friends/sam.vcf" -H "Content-Type: text/vcard" --data-binary $'BEGIN:VCARD\r\nVERSION:3.0\r\nN:Cohen;Sam;;;\r\nFN:Sam Cohen\r\nEMAIL:sam@example.net\r\nCATEGORIES:Atari Club\r\nUID:SAM-1\r\nEND:VCARD\r\n' >/dev/null
$CLI "$D" sync
R=$($CLI "$D" list)
expect "Sam synced" "Sam Cohen | sam@example.net" "$R"

echo "groups: a group card, and a category"
$CLI "$D" group-new "חברים" Family
$CLI "$D" group-add Family "Dana Levi"
$CLI "$D" group-add Family "Erez Yaary"
R=$($CLI "$D" groups)
expect "the group card" "Family (card, 2): Dana Levi, Erez Yaary" "$R"
expect "the category" "Atari Club (category, 1): Sam Cohen" "$R"
$CLI "$D" group-add "Atari Club" "Erez Yaary"
R=$($CLI "$D" groups)
expect "joined the category" "Atari Club (category, 2): Sam Cohen, Erez Yaary" "$R"
$CLI "$D" group-remove Family "Dana Levi"
R=$($CLI "$D" groups)
expect "left the group" "Family (card, 1): Erez Yaary" "$R"
echo "PEOPLE: EMail and ECal asking (ui/answer.c)"
R=$($CLI "$D" ask "names:EMAIL dana@example.org NOBODY@x.org DANA@HOME.EXAMPLE")
expect "names: to the asker" "to 7: names:" "$R"
expect "a known address, its name" $'dana@example.org\tDana Levi' "$R"
expect "any case" $'DANA@HOME.EXAMPLE\tDana Levi' "$R"
if grep -qx $'NOBODY@x.org\t' <<<"$R"; then ok "an unknown one, empty"; else bad "an unknown one: $R"; fi
R=$($CLI "$D" ask "pick:EMAIL dan" @group Family)
expect "pick: the search for the window" 'pick for EMAIL, searching "dan"' "$R"
expect "pick: answered" "to 7: pick:" "$R"
expect "a group: its members with an address" $'erez@example.com\tErez Yaary' "$R"
R=$($CLI "$D" ask "names:EMAIL sam@example.net" @poll @poll)
N=$(grep -c "changed:" <<<"$R" || true)
if [ "$N" = 1 ]; then ok "changed: once, when the names are first there"; else bad "changed: $N times"; fi
R=$($CLI "$D" ask "names:EMAIL sam@example.net" @poll @email "Dana Levi" dana@new.example @poll)
N=$(grep -c "changed:" <<<"$R" || true)
if [ "$N" = 2 ]; then ok "and again when an address changed"; else bad "changed: $N times"; fi
R=$($CLI "$D" ask "names:EMAIL dana@new.example")
expect "the new address known" $'dana@new.example\tDana Levi' "$R"
$CLI "$D" group-rename "Atari Club" "Atari Friends"
R=$($CLI "$D" groups)
expect "renamed category" "Atari Friends (category, 2)" "$R"

echo "import and export"
printf 'BEGIN:VCARD\r\nVERSION:2.1\r\nN;CHARSET=UTF-8;ENCODING=QUOTED-PRINTABLE:M=C3=BCller;J=C3=BCrgen\r\nTEL;CELL:0170 1234567\r\nEND:VCARD\r\nBEGIN:VCARD\r\nVERSION:3.0\r\nFN:Atari Corp\r\nORG:Atari Corp\r\nX-ABShowAs:COMPANY\r\nEND:VCARD\r\n' > "$W/in.vcf"
R=$($CLI "$D" import "חברים" "$W/in.vcf")
expect "two imported" "2 imported" "$R"
R=$($CLI "$D" list)
expect "the 2.1 card, in 3.0 now" "Jürgen Müller | 0170 1234567" "$R"
expect "the company" "Atari Corp" "$R"
$CLI "$D" export "$W/out.vcf"
R=$(cat "$W/out.vcf")
expect "exported" "FN:Jürgen Müller" "$R"

echo "delete"
$CLI "$D" delete "Erez Yaary"
R=$($CLI "$D" list)
absent "deleted here" "Erez Yaary" "$R"
R=$("${CURL[@]}" -X PROPFIND -H "Depth: 1" "$BASE/alice/friends/")
R2=$($CLI "$D" groups)
expect "and out of its group" "Family (card, 0)" "$R2"
$CLI "$D" sync force
R=$($CLI "$D" list)
absent "and on the server" "Erez Yaary" "$R"
expect "the others still there" "Dana Levi" "$R"

echo "photos: decoded once, kept, and made again when they change"
if python3 -c "import PIL" 2>/dev/null; then
  # a JPEG of four quarters (TL TR / BL BR), or one colour
  mkjpg() { python3 - "$@" <<'PY'
import sys
from PIL import Image
out, spec = sys.argv[1], sys.argv[2:]
cols = [tuple(int(v) for v in c.split(",")) for c in spec]
im = Image.new("RGB", (240, 240))
px = im.load()
for y in range(240):
    for x in range(240):
        px[x, y] = cols[0] if len(cols) == 1 else cols[(y >= 120) * 2 + (x >= 120)]
im.save(out, "JPEG", quality=90)
PY
  }
  putcard() { # putcard FILE.vcf UID NAME PHOTO-LINE [EXTRA]
    printf 'BEGIN:VCARD\r\nVERSION:3.0\r\nN:%s;Pat;;;\r\nFN:Pat %s\r\n%s\r\n%bUID:%s\r\nEND:VCARD\r\n' \
      "$3" "$3" "$4" "${5:-}" "$2" > "$W/$1"
    "${CURL[@]}" -X PUT "$BASE/alice/friends/$1" -H "Content-Type: text/vcard" --data-binary @"$W/$1" >/dev/null
  }
  b64() { python3 -c "import base64,sys;d=base64.b64encode(open(sys.argv[1],'rb').read()).decode();print('\r\n '.join(d[i:i+74] for i in range(0,len(d),74)))" "$1"; }
  mkjpg "$W/q.jpg" 230,30,30 30,200,40 30,40,220 240,220,20
  mkjpg "$W/blue.jpg" 20,40,230
  putcard pat.vcf PAT-1 Photo "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/q.jpg")"
  $CLI "$D" sync
  R=$($CLI "$D" photo "Pat Photo")
  expect "decoded the first time" "ok decoded" "$R"
  expect "the quarters in place" "TL=red TR=green BL=blue BR=yellow" "$R"
  R=$($CLI "$D" photo "Pat Photo")
  expect "from the cache after" "ok cache" "$R"
  $CLI "$D" set "Pat Photo" title "Photographer"
  R=$($CLI "$D" photo "Pat Photo")
  expect "a change to another field keeps the picture" "ok cache" "$R"
  R=$("${CURL[@]}" "$BASE/alice/friends/pat.vcf")
  expect "and the photo stays on the card" "PHOTO;ENCODING=b;TYPE=JPEG:/9j/" "$R"
  # a new photo, made on the iPhone
  putcard pat.vcf PAT-1 Photo "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/blue.jpg")"
  $CLI "$D" sync
  R=$($CLI "$D" photo "Pat Photo")
  expect "a changed photo is decoded again" "ok decoded" "$R"
  expect "the new one" "TL=blue TR=blue BL=blue BR=blue" "$R"
  # a progressive JPEG (TJpgDec won't), and a PNG (not shown, and said why)
  python3 -c "
from PIL import Image; import sys
im = Image.open(sys.argv[1]); im.save(sys.argv[2], 'JPEG', quality=85, progressive=True); im.save(sys.argv[3], 'PNG')" \
    "$W/q.jpg" "$W/qp.jpg" "$W/q.png"
  putcard prog.vcf PROG-1 Progressive "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/qp.jpg")"
  putcard png.vcf PNG-1 Png "PHOTO;ENCODING=b;TYPE=PNG:$(b64 "$W/q.png")"
  $CLI "$D" sync
  R=$($CLI "$D" photo "Pat Progressive")
  expect "a progressive JPEG decoded" "ok decoded TL=red TR=green BL=blue BR=yellow" "$R"
  R=$($CLI "$D" photo "Pat Png")
  expect "a PNG: not, and why" "failed decoded (a PNG picture)" "$R"
  R=$($CLI "$D" photo "Pat Png")
  expect "remembered" "failed cache (a PNG picture)" "$R"
  # a failure from an older decoder is tried again
  $CLI "$D" photo-dec "Pat Png" 1
  R=$($CLI "$D" photo "Pat Png")
  expect "an older decoder's failure tried again" "failed decoded" "$R"
  # as iCloud keeps a photo in the card: a long crop parameter, data:,
  # and the fold falling in the middle of "data:image/jpeg"
  python3 - "$W/q.jpg" "$W/icloud.vcf" <<'PY'
import base64, sys
d = base64.b64encode(open(sys.argv[1], "rb").read()).decode()
line = "PHOTO;X-ABCROP-RECTANGLE=ABClipRect_1&0&0&240&240&2gkd5u82KlA9jUBbH5vD7Q==;VALUE=uri:data:image/jpeg;base64," + d
k = line.index("data:") + 3
parts = [line[:k]] + [line[i:i + 74] for i in range(k, len(line), 74)]
card = "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Icloud;Pat;;;\r\nFN:Pat Icloud\r\n" + "\r\n ".join(parts) + \
       "\r\nUID:ICLOUD-1\r\nEND:VCARD\r\n"
open(sys.argv[2], "w", newline="").write(card)
PY
  # (into a local book: Radicale mangles a data: photo, cutting it at the comma)
  L=$W/local-icloud
  $CLI "$L" local Cards >/dev/null
  $CLI "$L" import Cards "$W/icloud.vcf" >/dev/null
  R=$(CARD_VERBOSE=1 $CLI "$L" photo "Pat Icloud" 2>/dev/null)
  expect "iCloud's way, folded in \"data:\"" "ok decoded TL=red TR=green BL=blue BR=yellow" "$R"
  "${CURL[@]}" -X DELETE "$BASE/alice/friends/prog.vcf" >/dev/null
  "${CURL[@]}" -X DELETE "$BASE/alice/friends/png.vcf" >/dev/null
  $CLI "$D" sync
  $CLI "$D" prune >/dev/null
  # the photo taken off the card
  putcard pat.vcf PAT-1 Photo "NOTE:no photo now"
  $CLI "$D" sync
  R=$($CLI "$D" photo "Pat Photo")
  expect "no photo any more" "none" "$R"
  R=$($CLI "$D" photo-file "Pat Photo")
  expect "and its picture gone" "missing" "$R"

  # a photo by link, as iCloud gives them
  PICPORT=$((PORT + 4))
  mkdir -p "$W/pics"
  cp "$W/q.jpg" "$W/pics/pic.jpg"
  python3 tests/host/picserver.py "$PICPORT" "$W/pics" 2> "$W/pics.log" &
  PIDS+=($!)
  for i in $(seq 50); do curl -s -o /dev/null "http://127.0.0.1:$PICPORT/pic.jpg" && break; sleep 0.2; done
  : > "$W/pics.log"
  LINK="PHOTO;VALUE=uri:http://127.0.0.1:$PICPORT/pic.jpg"
  putcard link.vcf LINK-1 Link "$LINK"
  $CLI "$D" sync
  R=$($CLI "$D" photo "Pat Link" offline)
  expect "offline: needs the network" "needs-net" "$R"
  R=$($CLI "$D" photo "Pat Link")
  expect "fetched" "ok fetched" "$R"
  expect "and decoded" "TL=red TR=green BL=blue BR=yellow" "$R"
  R=$($CLI "$D" photo "Pat Link")
  expect "then from the cache, without asking" "ok cache" "$R"
  expect "one request so far" "1" "$(grep -ac GET "$W/pics.log")"
  expect "no password to another server" "GET /pic.jpg 200 noauth" "$(tr -d '\0' < "$W/pics.log")"
  # the card changes, the picture doesn't: asked with If-None-Match
  putcard link.vcf LINK-1 Link "$LINK" "TITLE:Model\r\n"
  $CLI "$D" sync
  R=$($CLI "$D" photo "Pat Link")
  expect "the card changed: checked, 304" "ok checked" "$R"
  expect "the server said 304" "GET /pic.jpg 304" "$(tr -d '\0' < "$W/pics.log")"
  R=$($CLI "$D" photo "Pat Link")
  expect "then the cache again" "ok cache" "$R"
  # the same link, a new picture behind it
  cp "$W/blue.jpg" "$W/pics/pic.jpg"
  putcard link.vcf LINK-1 Link "$LINK" "TITLE:Model 2\r\n"
  $CLI "$D" sync
  R=$($CLI "$D" photo "Pat Link")
  expect "a new picture at the same link is fetched" "ok fetched" "$R"
  expect "and shown" "TL=blue TR=blue" "$R"
  # offline, a changed card still shows the picture it has
  putcard link.vcf LINK-1 Link "$LINK" "TITLE:Model 3\r\n"
  $CLI "$D" sync
  R=$($CLI "$D" photo "Pat Link" offline)
  expect "offline: the picture it has" "ok cache" "$R"
  # a contact deleted elsewhere: its picture pruned
  R=$($CLI "$D" photo-file "Pat Link")
  expect "its picture in the pack" "PHOTOS.PAK there" "$R"
  "${CURL[@]}" -X DELETE "$BASE/alice/friends/link.vcf" >/dev/null
  $CLI "$D" sync
  R=$($CLI "$D" prune)
  expect "pruned" "1 removed" "$R"
  R=$($CLI "$D" prune)
  expect "and gone from the pack" "0 removed" "$R"
  if [ -d "$D/CACHE/PHOTOS" ]; then bad "a PHOTOS folder"; else ok "one file, no PHOTOS folder"; fi
  ls "$D/CACHE" > "$W/cache.ls"
  expect "the pictures in PHOTOS.PAK" "PHOTOS.PAK" "$(cat "$W/cache.ls")"

  echo "photos left for later: cards synced without them (partial retrieval)"
  PPORT=$((PORT + 6))
  python3 tests/host/partialproxy.py "$PPORT" "$BASE" 2> "$W/proxy.log" &
  PIDS+=($!)
  RPORT=$((PORT + 8))
  python3 tests/host/partialproxy.py "$RPORT" "$BASE" --refuse 2> "$W/refuse.log" &
  PIDS+=($!)
  for i in $(seq 50); do curl -s -o /dev/null "http://127.0.0.1:$PPORT/" && curl -s -o /dev/null "http://127.0.0.1:$RPORT/" && break; sleep 0.2; done
  for n in 1 2 3 4 5 6; do
    putcard "p$n.vcf" "P-$n" "Many$n" "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/q.jpg")" "EMAIL:many$n@example.org\r\n"
  done
  putcard social.vcf SOC-1 Social "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/blue.jpg")" \
    "X-SOCIALPROFILE;TYPE=twitter:https://twitter.com/pat\r\nIMPP;X-SERVICE-TYPE=Skype:skype:pat.social\r\n"
  P=$W/partial
  $CLI "$P" carddav Part "http://127.0.0.1:$PPORT/" alice wonderland
  expect "asked without photos" "REPORT partial" "$(tr -d '\0' < "$W/proxy.log")"
  absent "and nothing whole" "REPORT whole" "$(tr -d '\0' < "$W/proxy.log")"
  R=$($CLI "$P" cache "Pat Many1")
  expect "kept in part, the photo still to come" "partial photo=unknown" "$R"
  R=$($CLI "$P" list)
  expect "the card is all there to see" "Pat Many1 | many1@example.org" "$R"
  ID=$(sed -n 's/^ID=//p' "$P/ECONTACT.INF" | tr -d '\r' | head -1)
  DAT=$P/CACHE/BOOK$(printf %04d "$ID").DAT
  PHO=${DAT%.DAT}.PHO
  SIZE=$(stat -c %s "$DAT")
  if [ "$SIZE" -lt 6000 ]; then ok "the cache is small ($SIZE bytes)"; else bad "the cache is $SIZE bytes"; fi
  if [ -e "$PHO" ]; then bad "a photo file already"; else ok "no photos kept yet"; fi
  R=$($CLI "$P" photo "Pat Many1" offline)
  expect "offline: needs the network" "needs-net" "$R"
  : > "$W/proxy.log"
  R=$($CLI "$P" photo "Pat Many1")
  expect "opened: the whole card fetched" "ok fetched" "$R"
  expect "and its photo shown" "TL=red TR=green BL=blue BR=yellow" "$R"
  expect "one card, whole" "REPORT whole 1 hrefs" "$(tr -d '\0' < "$W/proxy.log")"
  R=$($CLI "$P" cache "Pat Many1")
  expect "now kept whole, its photo apart" "whole photo=" "$R"
  if [ -s "$PHO" ]; then ok "the photo file"; else bad "no photo file"; fi
  R=$($CLI "$P" photo "Pat Many1")
  expect "then from the cache" "ok cache" "$R"
  # a change to a card kept in part: fetched whole first, nothing lost
  $CLI "$P" set "Pat Social" title "Influencer"
  R=$("${CURL[@]}" "$BASE/alice/friends/social.vcf")
  expect "the change saved" "TITLE:Influencer" "$R"
  expect "the lines EContact doesn't show kept" "X-SOCIALPROFILE;TYPE=twitter:https://twitter.com/pat" "$R"
  expect "and the other one" "IMPP;X-SERVICE-TYPE=Skype:skype:pat.social" "$R"
  expect "and the photo" "PHOTO;ENCODING=b;TYPE=JPEG:/9j/" "$R"
  absent "the stand-in never leaves" "X-ECONTACT-PHOTO" "$R"
  R=$($CLI "$P" cache "Pat Social")
  expect "kept whole after" "whole photo=" "$R"
  # Sync Contact Photos: the rest, and stopped half way it goes on later
  R=$($CLI "$P" photo-sync 2)
  expect "stopped after two" "made 2" "$R"
  R=$($CLI "$P" photo-sync)
  expect "the rest, another time" "made 4 failed 0" "$R"
  expect "by name, as it goes" "Pat Many6" "$R"
  R=$($CLI "$P" photo-sync)
  expect "then nothing left to do" "made 0 failed 0" "$R"
  R=$($CLI "$P" cache "Pat Many4")
  expect "all whole now" "whole photo=" "$R"
  R=$($CLI "$P" photo "Pat Social" offline)
  expect "and its picture made" "ok cache TL=blue" "$R"
  $CLI "$P" export "$W/part.vcf"
  R=$(cat "$W/part.vcf")
  expect "exported with the photos" "PHOTO;ENCODING=b;TYPE=JPEG:/9j/" "$R"
  absent "and without the stand-in" "X-ECONTACT-PHOTO" "$R"
  # a card we have whole, changed elsewhere: fetched whole, photo and all
  : > "$W/proxy.log"
  putcard p2.vcf P-2 Many2 "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/blue.jpg")" "EMAIL:many2@example.org\r\n"
  $CLI "$P" sync
  expect "a changed card fetched whole" "REPORT whole 1 hrefs" "$(tr -d '\0' < "$W/proxy.log")"
  R=$($CLI "$P" photo "Pat Many2" offline)
  expect "its new photo, without asking again" "ok decoded TL=blue" "$R"

  # TEMPORARY (src/photomig.c, 1.50): 1.49's CACHE\PHOTOS, a .PIC a
  # picture, moved into the pack the first time. Goes with photomig.c
  echo "1.49's pictures moved into PHOTOS.PAK"
  python3 - "$P/CACHE" <<'PY'
import os, struct, sys
cache = sys.argv[1]
pak = open(os.path.join(cache, "PHOTOS.PAK"), "rb").read()
off, n = struct.unpack(">II", pak[8:16])
os.makedirs(os.path.join(cache, "PHOTOS"))
for i in range(n):
    name, key, cetag, roff, rlen = struct.unpack(">IIIII", pak[off + 28 * i:off + 28 * i + 20])
    open(os.path.join(cache, "PHOTOS", "P%07X.PIC" % name), "wb").write(pak[roff:roff + rlen])
os.remove(os.path.join(cache, "PHOTOS.PAK"))
print(n)
PY
  N=$(ls "$P/CACHE/PHOTOS" | wc -l)
  if [ "$N" -ge 6 ]; then ok "1.49's files ($N)"; else bad "only $N .PIC files"; fi
  R=$($CLI "$P" photo "Pat Many3" offline)
  expect "a picture from 1.49's file, as it was" "ok cache TL=red TR=green BL=blue BR=yellow" "$R"
  R=$($CLI "$P" photo "Pat Social" offline)
  expect "and another" "ok cache TL=blue" "$R"
  if [ -d "$P/CACHE/PHOTOS" ]; then bad "the PHOTOS folder is still there"; else ok "the PHOTOS folder gone"; fi
  if [ -s "$P/CACHE/PHOTOS.PAK" ]; then ok "PHOTOS.PAK made"; else bad "no PHOTOS.PAK"; fi
  R=$($CLI "$P" photo-sync)
  expect "nothing left to make after" "made 0 failed 0" "$R"

  echo "a server that refuses partial retrieval; little memory"
  noise() { python3 -c "
from PIL import Image; import os, sys
s = int(sys.argv[2]); Image.frombytes('RGB', (s, s), os.urandom(s * s * 3)).save(sys.argv[1], 'JPEG', quality=90)" "$1" "$2"; }
  noise "$W/big.jpg" 160
  noise "$W/huge.jpg" 300
  putcard big.vcf BIG-1 Big "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/big.jpg")"
  putcard big2.vcf BIG-2 Bigger "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/big.jpg")"
  putcard huge.vcf HUGE-1 Huge "PHOTO;ENCODING=b;TYPE=JPEG:$(b64 "$W/huge.jpg")"
  Q=$W/refused
  CARD_BUDGET=20000 $CLI "$Q" carddav Refused "http://127.0.0.1:$RPORT/" alice wonderland
  R=$($CLI "$Q" cache "Pat Many3")
  expect "the whole cards instead" "whole photo=" "$R"
  N=$(tr -d '\0' < "$W/refuse.log" | grep -c "REPORT whole" || true)
  if [ "$N" -ge 3 ]; then ok "a few at a time ($N answers)"; else bad "only $N answers"; fi
  R=$($CLI "$Q" list)
  expect "the big ones too" "Pat Bigger" "$R"
  absent "one too big for the memory left for next time" "Pat Huge" "$R"
  for n in 1 2 3 4 5 6; do expect "Pat Many$n there" "Pat Many$n | many$n@example.org" "$R"; done
  R=$($CLI "$Q" photo "Pat Many5" offline)
  expect "its photo from the cache files" "ok decoded TL=red" "$R"

  echo "a server that sends the whole card but not the photo"
  BPORT=$((PORT + 10))
  python3 tests/host/partialproxy.py "$BPORT" "$BASE" --blank-photos 2> "$W/blank.log" &
  PIDS+=($!)
  for i in $(seq 50); do curl -s -o /dev/null "http://127.0.0.1:$BPORT/" && break; sleep 0.2; done
  B=$W/blank
  $CLI "$B" carddav Blank "http://127.0.0.1:$BPORT/" alice wonderland
  R=$($CLI "$B" cache "Pat Many4")
  expect "kept as in part all the same" "partial photo=unknown" "$R"
  $CLI "$B" set "Pat Many4" title "Kept"
  R=$("${CURL[@]}" "$BASE/alice/friends/p4.vcf")
  expect "changed" "TITLE:Kept" "$R"
  expect "its photo still on the server" "PHOTO;ENCODING=b;TYPE=JPEG:/9j/" "$R"

  echo "EContact 1.9's cache, converted"
  # written as 1.9 did: the photos in the cards, three fields a record
  python3 - "$DAT" <<'PY'
import re, sys
dat = sys.argv[1]
pho = dat[:-4] + ".PHO"
d = open(dat, "rb").read()
p = open(pho, "rb").read()
out, i = [b"CARDCACHE 1\n"], d.index(b"\n") + 1
while i < len(d):
    j = d.index(b"\n", i)
    head = d[i:j].decode()
    i = j + 1
    if head.startswith("X "):
        out.append((head + "\n").encode())
        continue
    etag, href, n, flag, poff, plen = head[2:].split("\t")
    card = d[i:i + int(n)]
    i += int(n) + 1
    if int(plen):
        card = re.sub(rb"X-ECONTACT-PHOTO:[0-9A-F]+\r\n", lambda m: p[int(poff):int(poff) + int(plen)], card)
    out.append(("R %s\t%s\t%d\n" % (etag, href, len(card))).encode() + card + b"\n")
open(dat, "wb").write(b"".join(out))
import os
os.remove(pho)
PY
  expect "the old kind" "CARDCACHE 1" "$(head -1 "$DAT")"
  R=$($CLI "$P" list)
  expect "read" "Pat Many1 | many1@example.org" "$R"
  expect "and written anew" "CARDCACHE 2" "$(head -1 "$DAT")"
  if [ -s "$PHO" ]; then ok "the photos apart"; else bad "no photo file after converting"; fi
  R=$($CLI "$P" photo "Pat Many1" offline)
  expect "the pictures made before still good" "ok cache" "$R"
  R=$($CLI "$P" cache "Pat Many6")
  expect "the cards as they were" "whole photo=" "$R"
else
  echo "  (skipped: needs Pillow)"
fi

echo "many contacts, little memory: the server's list read as it comes"
"${CURL[@]}" -X MKCOL "$BASE/alice/many/" -H "Content-Type: application/xml" --data-binary @- >/dev/null <<XML
<?xml version="1.0" encoding="utf-8"?>
<D:mkcol xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:carddav"><D:set><D:prop>
<D:resourcetype><D:collection/><C:addressbook/></D:resourcetype><D:displayname>Many</D:displayname>
</D:prop></D:set></D:mkcol>
XML
python3 - "$BASE/alice/many/" <<'PY'
import base64, sys, urllib.request
base = sys.argv[1]
auth = "Basic " + base64.b64encode(b"alice:wonderland").decode()
for i in range(300):
    card = ("BEGIN:VCARD\r\nVERSION:3.0\r\nN:Member%03d;Club;;;\r\nFN:Club Member%03d\r\n"
            "EMAIL:m%03d@example.org\r\nUID:MANY-%03d\r\nEND:VCARD\r\n" % (i, i, i, i))
    rq = urllib.request.Request(base + "m%03d-with-a-long-name-like-icloud-ones.vcf" % i, data=card.encode(),
                                method="PUT", headers={"Content-Type": "text/vcard", "Authorization": auth})
    urllib.request.urlopen(rq).read()
PY
M=$W/many
CARD_BUDGET=20000 CARD_LOG=$W/many.log $CLI "$M" carddav Many "$BASE/" alice wonderland
R=$($CLI "$M" books)
expect "all 300, the list read in pieces" "Many (Many) 300 contacts" "$R"
"${CURL[@]}" -X DELETE "$BASE/alice/many/m007-with-a-long-name-like-icloud-ones.vcf" >/dev/null
"${CURL[@]}" -X PUT "$BASE/alice/many/m100-with-a-long-name-like-icloud-ones.vcf" -H "Content-Type: text/vcard" \
  --data-binary $'BEGIN:VCARD\r\nVERSION:3.0\r\nN:Changed;Club;;;\r\nFN:Club Changed\r\nUID:MANY-100\r\nEND:VCARD\r\n' >/dev/null
CARD_BUDGET=20000 $CLI "$M" sync
R=$($CLI "$M" books)
expect "one gone" "Many (Many) 299 contacts" "$R"
R=$($CLI "$M" list)
expect "one changed" "Club Changed" "$R"
absent "the gone one" "Club Member007" "$R"
expect "the others kept" "Club Member299" "$R"

echo "the parsed cache (BOOKnnnn.IDX)"
MID=$(sed -n 's/^ID=//p' "$M/ECONTACT.INF" | tr -d '\r' | tail -1)
IDX=$M/CACHE/BOOK$(printf %04d "$MID").IDX
if [ -s "$IDX" ]; then ok "kept"; else bad "no $IDX"; fi
BEFORE=$($CLI "$M" list | md5sum)
# another parser's: its header says so (parse_id), and it isn't used
python3 -c "import sys; d=bytearray(open(sys.argv[1],'rb').read()); d[12:16]=b'P0.X'; open(sys.argv[1],'wb').write(d)" "$IDX"
R=$($CLI "$M" list | md5sum)
if [ "$R" = "$BEFORE" ]; then ok "another parser's: the same contacts"; else bad "another parser's cache changed the list"; fi
if head -c 28 "$IDX" | grep -q host; then ok "and written again for this one"; else bad "another parser's cache kept"; fi
# cut short, as by a crash while it was written
head -c 5000 "$IDX" > "$IDX.x" && mv "$IDX.x" "$IDX"
R=$($CLI "$M" list | md5sum)
if [ "$R" = "$BEFORE" ]; then ok "cut short: the same"; else bad "cut cache changed the list"; fi
if [ "$(stat -c %s "$IDX")" -gt 1000 ]; then ok "and written again"; else bad "not written again"; fi
# made from this copy of BOOKnnnn.DAT (the number on its first line):
# the cards found by where they are, nothing hashed
DAT=${IDX%.IDX}.DAT
R=$(CARD_LOG=$W/idx1.log $CLI "$M" list | md5sum)
if [ "$R" = "$BEFORE" ]; then ok "from the same file: the same"; else bad "the list changed"; fi
expect "and not hashed" "(not hashed)" "$(grep -a "Many: " "$W/idx1.log")"
# another copy (its number changed): hashed, then written again for it
python3 -c "import sys; d=bytearray(open(sys.argv[1],'rb').read()); assert d[:12]==b'CARDCACHE 2 '; d[12:20]=b'0BADCAFE'; open(sys.argv[1],'wb').write(d)" "$DAT"
R=$(CARD_LOG=$W/idx2.log $CLI "$M" list | md5sum)
if [ "$R" = "$BEFORE" ]; then ok "another copy: the same"; else bad "another copy changed the list"; fi
absent "and hashed" "(not hashed)" "$(grep -a "Many: " "$W/idx2.log")"
expect "and the index written again" "the index written" "$(grep -a "Many: " "$W/idx2.log")"
R=$(CARD_LOG=$W/idx3.log $CLI "$M" list | md5sum)
expect "then not hashed again" "(not hashed)" "$(grep -a "Many: " "$W/idx3.log")"
# a card changed on the server: added to the end of the file, which keeps
# its number, and the index finds it by where it is too
"${CURL[@]}" -X PUT "$BASE/alice/many/m200-with-a-long-name-like-icloud-ones.vcf" -H "Content-Type: text/vcard" \
  --data-binary $'BEGIN:VCARD\r\nVERSION:3.0\r\nN:Again;Club;;;\r\nFN:Club Again\r\nUID:MANY-200\r\nEND:VCARD\r\n' >/dev/null
CARD_BUDGET=20000 $CLI "$M" sync >/dev/null
R=$(CARD_LOG=$W/idx7.log $CLI "$M" list)
expect "a changed card" "Club Again" "$R"
absent "its old self" "Club Member200" "$R"
expect "added to: not hashed" "(not hashed)" "$(grep -a "Many: " "$W/idx7.log")"
expect "nothing parsed" " 0 parsed" "$(grep -a "Many: " "$W/idx7.log")"
BEFORE=$($CLI "$M" list | md5sum)
# 1.39's file, without a number: its size and date instead
python3 -c "import sys; d=open(sys.argv[1],'rb').read(); i=d.index(b'\n'); open(sys.argv[1],'wb').write(b'CARDCACHE 2'+d[i:])" "$DAT"
R=$(CARD_LOG=$W/idx4.log $CLI "$M" list | md5sum)
if [ "$R" = "$BEFORE" ]; then ok "without a number: the same"; else bad "without a number: the list changed"; fi
absent "hashed" "(not hashed)" "$(grep -a "Many: " "$W/idx4.log")"
R=$(CARD_LOG=$W/idx5.log $CLI "$M" list | md5sum)
if [ "$R" = "$BEFORE" ]; then ok "then by its size and date: the same"; else bad "by size and date: the list changed"; fi
expect "and not hashed" "(not hashed)" "$(grep -a "Many: " "$W/idx5.log")"
# 1.39's index (CARDIDX, no offsets): read by the hashes, written anew
python3 - "$IDX" <<'PY'
import sys, struct
d = open(sys.argv[1], 'rb').read()
# this machine's (the host's) layout: 8-byte longs; the entry's off last
H, E = 8 + 8 + 16, 8 * 3 + 2 + 2 + 4
out, i = [b"CARDIDX\n" + d[8:H]], H + 24
while i + E + 8 <= len(d):
    blen = struct.unpack("<q", d[i + 16:i + 24])[0]
    n = blen + (blen & 1)
    out.append(d[i:i + E] + d[i + E + 8:i + E + 8 + n])
    i += E + 8 + n
open(sys.argv[1], "wb").write(b"".join(out))
PY
R=$(CARD_LOG=$W/idx6.log $CLI "$M" list | md5sum)
if [ "$R" = "$BEFORE" ]; then ok "1.39's index: the same"; else bad "1.39's index changed the list"; fi
expect "used" " 0 parsed" "$(grep -a "Many: " "$W/idx6.log")"
if head -c 8 "$IDX" | grep -q CARDIDX2; then ok "and written anew"; else bad "1.39's index kept"; fi

echo "the accessory while the program runs (single TOS)"
# the accessory read the files; ECONTACT.PRG then ran and synced, writing
# the cache anew; the accessory, opened again, must not copy cards from
# the old places in the new file, or write its old settings over the new
BOB=(curl -s -u bob:builder)
"${BOB[@]}" -X MKCOL "$BASE/bob/acc/" -H "Content-Type: application/xml" --data-binary @- >/dev/null <<XML
<?xml version="1.0" encoding="utf-8"?>
<D:mkcol xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:carddav"><D:set><D:prop>
<D:resourcetype><D:collection/><C:addressbook/></D:resourcetype><D:displayname>Acc</D:displayname>
</D:prop></D:set></D:mkcol>
XML
for n in Aaron Bella Chris Dora; do
  printf 'BEGIN:VCARD\r\nVERSION:3.0\r\nN:%s;Person;;;\r\nFN:Person %s\r\nEMAIL:%s@example.com\r\nUID:%s\r\nEND:VCARD\r\n' $n $n $n $n |
    "${BOB[@]}" -X PUT "$BASE/bob/acc/$n.vcf" -H "Content-Type: text/vcard" --data-binary @- >/dev/null
done
A=$W/acc
$CLI "$A" carddav Bob "$BASE/" bob builder >/dev/null
mkfifo "$W/accfifo"
$CLI "$A" accessory < "$W/accfifo" > "$W/acc.out" 2>&1 &
APID=$!
exec 3>"$W/accfifo"
for i in $(seq 50); do grep -q waiting "$W/acc.out" && break; sleep 0.1; done
"${BOB[@]}" -X DELETE "$BASE/bob/acc/Aaron.vcf" >/dev/null
printf 'BEGIN:VCARD\r\nVERSION:3.0\r\nN:Evans;Eve;;;\r\nFN:Eve Evans\r\nEMAIL:eve@example.com\r\nNOTE:a longer card, so that the file is longer than before\r\nUID:Eve\r\nEND:VCARD\r\n' |
  "${BOB[@]}" -X PUT "$BASE/bob/acc/Eve.vcf" -H "Content-Type: text/vcard" --data-binary @- >/dev/null
$CLI "$A" sync >/dev/null
$CLI "$A" local Kept >/dev/null	# a book the program added: in ECONTACT.INF
echo go >&3
exec 3>&-
wait $APID
expect "the accessory saw the program's changes" "the files changed: read again" "$(cat "$W/acc.out")"
R=$($CLI "$A" list)
for n in Bella Chris Dora; do expect "$n kept" "Person $n | $n@example.com" "$R"; done
expect "the new card" "Eve Evans | eve@example.com" "$R"
absent "the deleted card" "Aaron" "$R"
expect "the program's book kept" "Kept" "$($CLI "$A" books)"
if ! grep -a "EGIN:VCARD" "$A"/CACHE/BOOK*.DAT | grep -qv "^BEGIN:VCARD"; then
  ok "the cache's cards whole"; else bad "a card in the cache cut"; fi
# opened again with nothing changed in between: not read again
mkfifo "$W/accfifo2"
$CLI "$A" accessory < "$W/accfifo2" > "$W/acc2.out" 2>&1 &
APID=$!
exec 3>"$W/accfifo2"
for i in $(seq 50); do grep -q waiting "$W/acc2.out" && break; sleep 0.1; done
echo go >&3
exec 3>&-
wait $APID
expect "nothing changed: kept" "the files as they were" "$(cat "$W/acc2.out")"

echo "refreshing in the background (the accessory, its window closed)"
"${BOB[@]}" -X MKCOL "$BASE/bob/quiet/" -H "Content-Type: application/xml" --data-binary @- >/dev/null <<XML
<?xml version="1.0" encoding="utf-8"?>
<D:mkcol xmlns:D="DAV:" xmlns:C="urn:ietf:params:xml:ns:carddav"><D:set><D:prop>
<D:resourcetype><D:collection/><C:addressbook/></D:resourcetype><D:displayname>Quiet</D:displayname>
</D:prop></D:set></D:mkcol>
XML
qcard() { printf 'BEGIN:VCARD\r\nVERSION:3.0\r\nN:%s;%s;;;\r\nFN:%s %s\r\nEMAIL:%s@example.net\r\nUID:Q-%s\r\nEND:VCARD\r\n' "$1" "$2" "$2" "$1" "$1" "$1" |
  "${BOB[@]}" -X PUT "$BASE/bob/quiet/$1.vcf" -H "Content-Type: text/vcard" --data-binary @- >/dev/null; }
for n in Ann Ben Cat Dan; do qcard $n Quiet; done
Q=$W/quiet
$CLI "$Q" carddav Bob "$BASE/" bob builder >/dev/null
QDAT=$(ls "$Q"/CACHE/BOOK*.DAT | head -1)
for f in "$Q"/CACHE/BOOK*.DAT; do grep -q "Q-Ann" "$f" && QDAT=$f; done
GEN=$(head -1 "$QDAT")
"${BOB[@]}" -X DELETE "$BASE/bob/quiet/Ben.vcf" >/dev/null
qcard Cat Catherine
qcard Eve Quiet
R=$($CLI "$Q" quiet-sync)
expect "a book changed" "1 book changed" "$R"
R=$($CLI "$Q" list)
expect "the new one" "Quiet Eve | Eve@example.net" "$R"
expect "the changed one" "Catherine Cat | Cat@example.net" "$R"
absent "its old self" "Quiet Cat |" "$R"
absent "the gone one" "Quiet Ben" "$R"
expect "the others kept" "Quiet Dan | Dan@example.net" "$R"
expect "added to the cache, not written anew" "$GEN" "$(head -1 "$QDAT")"
expect "its names known" $'Eve@example.net\tQuiet Eve' "$($CLI "$Q" ask "names:EMAIL Eve@example.net")"
R=$($CLI "$Q" quiet-sync)
expect "nothing changed: nothing done" "0 books changed" "$R"
R=$($CLI "$Q" sync && $CLI "$Q" list)
expect "a sync after it: the same" "Catherine Cat | Cat@example.net" "$R"
absent "and Ben still gone" "Quiet Ben" "$R"

echo "On My Atari"
$CLI "$D" local Notes
$CLI "$D" add Notes Local Person local@example.com
R=$($CLI "$D" list)
expect "a local contact" "Local Person | local@example.com" "$R"

if [ "$fails" -gt 0 ]; then echo "$fails failed"; exit 1; fi
echo "all passed"
