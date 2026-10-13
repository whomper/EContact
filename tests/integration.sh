#!/bin/bash
# EContact's core against a real CardDAV server (Radicale), with card-cli
# (the same abook/CardDAV/HTTP/TLS code as the Atari):
#
#   tests/integration.sh             plain HTTP, straight to the server
#   GATEWAY=path/to/ecalgate.py tests/integration.sh
#                                    through ECal's Pi gateway
#
# Needs: pip install radicale.
set -euo pipefail
cd "$(dirname "$0")/.."
CLI=$PWD/build/card-cli
PEOPLE=$PWD/build/people-cli
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

echo "alice:wonderland" > "$W/users"
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
R=$($CLI "$D" people)
expect "CONTACTS.TXT: a person" $'P\tDana Levi\tdana@example.org\twork' "$R"
expect "CONTACTS.TXT: a group" $'G\tFamily\tErez Yaary <erez@example.com>' "$R"
$CLI "$D" group-rename "Atari Club" "Atari Friends"
R=$($CLI "$D" groups)
expect "renamed category" "Atari Friends (category, 2)" "$R"

echo "the people for EMail and ECal (share/people.c)"
R=$($PEOPLE "$D/ECONTACT.INF" "")
expect "everyone" "Dana Levi <dana@example.org>" "$R"
expect "the group first" "1	Family (1 person)	Erez Yaary <erez@example.com>" "$R"
R=$($PEOPLE "$D/ECONTACT.INF" "dan")
expect "a search" "Dana Levi <dana@home.example>" "$R"
absent "only matches" "Sam Cohen" "$R"
R=$($PEOPLE "$D/ECONTACT.INF" "-complete" "Bob <b@x.org>, da" "Dana Levi <dana@example.org>")
expect "completing a To: line" "Bob <b@x.org>, Dana Levi <dana@example.org>" "$R"
R=$($PEOPLE "$D/ECONTACT.INF" "-complete" "Bob <b@x.org>" "Sam <s@x.org>")
expect "adding to a To: line" "Bob <b@x.org>, Sam <s@x.org>" "$R"

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
  F=$($CLI "$D" photo-file "Pat Link" | cut -d' ' -f1)
  "${CURL[@]}" -X DELETE "$BASE/alice/friends/link.vcf" >/dev/null
  $CLI "$D" sync
  R=$($CLI "$D" prune)
  expect "pruned" "1 removed" "$R"
  if [ -e "$F" ]; then bad "the picture file is still there"; else ok "the picture file is gone"; fi
else
  echo "  (skipped: needs Pillow)"
fi

echo "On My Atari"
$CLI "$D" local Notes
$CLI "$D" add Notes Local Person local@example.com
R=$($CLI "$D" list)
expect "a local contact" "Local Person | local@example.com" "$R"

if [ "$fails" -gt 0 ]; then echo "$fails failed"; exit 1; fi
echo "all passed"
