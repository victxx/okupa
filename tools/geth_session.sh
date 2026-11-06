#!/bin/sh
# A session with a real Geth on this machine: start a Sepolia node that neither
# syncs nor discovers (no blocks are needed for the handshake), dial it with
# build/okupa-peer and keep both sides' logs as evidence. Nothing is sent.
#   GETH=/path/to/geth sh tools/geth_session.sh [run/<dir>]
set -e
geth=${GETH:-geth}
dir=${1:-run/geth-session-$(date +%Y%m%d-%H%M%S)}
mkdir -p "$dir"
"$geth" --sepolia --datadir "$dir/data" --port 30399 --nodiscover --maxpeers 4 --syncmode snap \
    --verbosity 4 --log.file "$dir/geth.log" --ipcpath "$dir/geth.ipc" > "$dir/geth-stdout.txt" 2>&1 &
pid=$!
trap 'kill $pid 2>/dev/null; wait $pid 2>/dev/null' EXIT
i=0
until [ -S "$dir/geth.ipc" ]; do
    i=$((i + 1)); [ $i -lt 60 ] || { echo "geth did not start"; exit 1; }; sleep 1
done
enode=$("$geth" attach --exec admin.nodeInfo.enode "$dir/geth.ipc" | tr -d '"' | sed 's/?.*//; s/@[^:]*:/@127.0.0.1:/')
"$geth" version > "$dir/geth-version.txt"
./build/okupa-peer "$enode" | tee "$dir/okupa-peer.txt"
sleep 1
grep -E "name=okupa/|req=true" "$dir/geth.log" | tee "$dir/geth-side.txt"
grep -q "Ethereum peer connected.*name=okupa/" "$dir/geth.log"
grep -q "PASS session complete" "$dir/okupa-peer.txt"
echo "geth session: both sides logged the eth session in $dir"
