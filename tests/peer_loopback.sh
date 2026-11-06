#!/bin/sh
# Two host peers over loopback: one answers, one dials, both must finish the
# handshake, Hello, Status and Ping/Pong. Proves both sides agree, not interop.
set -e
peer=$1
# The signed Sepolia hello from tests/test_tx.c (test key 0x46 * 32), to a loopback listener only.
tx=02f87483aa36a780843b9aca008506fc23ac0082f35c946e2c210710508fd15f37b12a2beb312ec4beaf1980864f4b50410100c080a0f50fc320e10ae34d46db9ce4eb0e4c5b3a4bac2fbfdcc3418433a43802e377bea06d75a300b7fc644bd03cc97f407e96baf895dc805dc5e55777391e39c9831ad4
port=$((30000 + $$ % 2000))
log=$(mktemp -d)
"$peer" --listen "$port" > "$log/listener.txt" 2>&1 &
pid=$!
i=0
while ! grep -q "listening" "$log/listener.txt" 2>/dev/null; do
    i=$((i + 1)); [ $i -lt 50 ] || { cat "$log/listener.txt"; exit 1; }; sleep 0.1
done
node=$(sed -n 's/.*node \([0-9a-f]*\)$/\1/p' "$log/listener.txt")
"$peer" --tx "$tx" "enode://$node@127.0.0.1:$port" > "$log/dialer.txt" 2>&1 || { cat "$log/dialer.txt" "$log/listener.txt"; exit 1; }
wait $pid || { cat "$log/dialer.txt" "$log/listener.txt"; exit 1; }
grep -q "PASS" "$log/dialer.txt" && grep -q "PASS" "$log/listener.txt"
grep -q "sent Transactions" "$log/dialer.txt" && grep -q "got message 0x12" "$log/listener.txt"
echo "peer: loopback dialer and listener both complete the session; the transaction goes out right after Status"
rm -rf "$log"
