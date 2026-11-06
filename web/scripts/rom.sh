#!/bin/sh
# Puts a built ROM on the landing page: public/download/okupa.nds and rom.json
# (commit, size, sha256). Usage: sh scripts/rom.sh <path to okupa.nds> <commit>
set -e
cd "$(dirname "$0")/.."
ROM=${1:?path to okupa.nds}; COMMIT=${2:?commit the ROM was built from}
cp "$ROM" public/download/okupa.nds
SHA=$(shasum -a 256 public/download/okupa.nds | cut -d' ' -f1)
SIZE=$(wc -c < public/download/okupa.nds | tr -d ' ')
printf '{ "commit": "%s", "bytes": %s, "sha256": "%s", "built": "%s" }\n' "$COMMIT" "$SIZE" "$SHA" "$(date -u +%Y-%m-%d)" > public/download/rom.json
cat public/download/rom.json
