#!/bin/sh
# Copies the generated C of zudiff's diff core into this package.
#
#   tools/sync-zudiff.sh /path/to/zudiff-checkout            # copy
#   tools/sync-zudiff.sh --verify /path/to/zudiff-checkout   # diff only
#
# The core is written in Goose in pedrobtz/zudiff (tools/goose/zudiff.goose)
# and compiled to C there, at the Goose commit in its tools/goose/PIN. This
# package takes that C unchanged; only the runtime and the CPython glue are
# its own. src/zugodiff/_goose/VENDORED records where the files came from.

set -eu

verify=0
if [ "${1:-}" = "--verify" ]; then
  verify=1
  shift
fi
if [ $# -ne 1 ]; then
  echo "usage: $0 [--verify] <zudiff-checkout>" >&2
  exit 2
fi

pkg=$(cd "$(dirname "$0")/.." && pwd)
zd=$(cd "$1" && pwd)
dest="$pkg/src/zugodiff/_goose"
files="zudiff_goose.c zudiff_goose.h goose_vendor.c goose_version.h"

for f in $files; do
  [ -f "$zd/src/$f" ] || { echo "no src/$f in $zd" >&2; exit 1; }
done

if [ "$verify" -eq 1 ]; then
  status=0
  for f in $files; do
    if ! cmp -s "$zd/src/$f" "$dest/$f"; then
      echo "src/zugodiff/_goose/$f differs from zudiff's src/$f" >&2
      status=1
    fi
  done
  [ "$status" -eq 0 ] && echo "vendored files match"
  exit "$status"
fi

for f in $files; do cp "$zd/src/$f" "$dest/$f"; done

{
  echo "# Written by tools/sync-zudiff.sh. Do not edit."
  echo "zudiff_commit $(git -C "$zd" rev-parse HEAD)"
  echo "goose_repository $(sed 's/\.git$//' "$zd/tools/goose/REMOTE" 2>/dev/null || echo https://github.com/aardappel/goose)"
  echo "goose_commit $(cat "$zd/tools/goose/PIN")"
  for f in $files; do
    echo "sha256 $(cd "$dest" && shasum -a 256 "$f")"
  done
} >"$dest/VENDORED"

echo "copied $files from zudiff $(git -C "$zd" rev-parse --short HEAD)"
