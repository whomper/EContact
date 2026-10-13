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

echo "On My Atari"
$CLI "$D" local Notes
$CLI "$D" add Notes Local Person local@example.com
R=$($CLI "$D" list)
expect "a local contact" "Local Person | local@example.com" "$R"

if [ "$fails" -gt 0 ]; then echo "$fails failed"; exit 1; fi
echo "all passed"
