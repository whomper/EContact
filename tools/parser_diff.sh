#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 AERA | Erez Yaary
# Whether src/vcard.c as it is reads cards as REV's did (default HEAD):
# both built with tests/host/vcdump.c, run over the cards
# tests/host/vcgen.py makes, and their output compared byte for byte; the
# new one with the address and undefined-behaviour sanitizers, and again
# over each card in a buffer of exactly its size (as abook.c hands them:
# not NUL-terminated). A change to the parser must pass it (CLAUDE.md).
#
#   tools/parser_diff.sh [REV]
set -e
cd "$(dirname "$0")/.."
rev=${1:-HEAD}
d=build/parser_diff
rm -rf "$d"
mkdir -p "$d/old"
git show "$rev:src/vcard.c" > "$d/old/vcard.c"
git show "$rev:src/vcard.h" > "$d/old/vcard.h"
python3 tests/host/vcgen.py "$d/cards"
CC=${HOSTCC:-cc}
SRC="common/src/charset.c common/src/util.c"
$CC -O1 -g -w -I"$d/old" -Isrc -Icommon/src -o "$d/dump-old" tests/host/vcdump.c "$d/old/vcard.c" $SRC
$CC -O1 -g -w -Isrc -Icommon/src -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-o "$d/dump-new" tests/host/vcdump.c src/vcard.c $SRC
$CC -O1 -g -w -DVCDUMP_EXACT -Isrc -Icommon/src -fsanitize=address,undefined -fno-sanitize-recover=undefined \
	-o "$d/dump-exact" tests/host/vcdump.c src/vcard.c $SRC
"$d/dump-old" "$d"/cards/* > "$d/old.txt"
"$d/dump-new" "$d"/cards/* > "$d/new.txt"
"$d/dump-exact" "$d"/cards/* > /dev/null
n=$(grep -c '^== ' "$d/old.txt")
if cmp -s "$d/old.txt" "$d/new.txt"; then
	echo "the same as $rev over $n files"
else
	echo "DIFFERENT from $rev (diff $d/old.txt $d/new.txt):"
	diff "$d/old.txt" "$d/new.txt" | head -20
	exit 1
fi
