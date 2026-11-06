#!/bin/sh
# Two host peers over loopback: one answers, one dials, both must finish the
# handshake, Hello, Status and Ping/Pong. Proves both sides agree, not interop.
set -e
peer=$1
port=$((30000 + $$ % 2000))
log=$(mktemp -d)
"$peer" --listen "$port" > "$log/listener.txt" 2>&1 &
pid=$!
i=0
while ! grep -q "listening" "$log/listener.txt" 2>/dev/null; do
    i=$((i + 1)); [ $i -lt 50 ] || { cat "$log/listener.txt"; exit 1; }; sleep 0.1
done
node=$(sed -n 's/.*node \([0-9a-f]*\)$/\1/p' "$log/listener.txt")
"$peer" "enode://$node@127.0.0.1:$port" > "$log/dialer.txt" 2>&1 || { cat "$log/dialer.txt" "$log/listener.txt"; exit 1; }
wait $pid || { cat "$log/dialer.txt" "$log/listener.txt"; exit 1; }
grep -q "PASS" "$log/dialer.txt" && grep -q "PASS" "$log/listener.txt"
echo "peer: loopback dialer and listener both complete the session"
rm -rf "$log"
