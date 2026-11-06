#!/bin/sh
# Local dev stack for Okupa web: Ethereum (Geth --dev), Swarm (Bee dev) and Arkiv (arkiv-reth --dev).
#   sh scripts/stack.sh up|down     GETH=/path/to/geth (1.17.7 tested)
# Everything here is local, in memory or under ../run/web-stack. Not public networks.
set -e
cd "$(dirname "$0")/.."
RUN=../run/web-stack
case "$1" in
up)
  mkdir -p $RUN
  : "${GETH:?set GETH to a geth binary}"
  "$GETH" --dev --dev.period 2 --ipcdisable --datadir $RUN/geth --http --http.addr 127.0.0.1 --http.port 8545 \
    --http.api eth,net,web3 --http.corsdomain '*' --http.vhosts '*' > $RUN/geth.log 2>&1 &
  echo $! > $RUN/geth.pid
  docker run -d --rm --name okupa-bee -p 127.0.0.1:1633:1633 ethersphere/bee:2.8.0 \
    dev --cors-allowed-origins='*' --api-addr=:1633 > /dev/null
  docker run -d --rm --name okupa-arkiv -p 127.0.0.1:8547:8545 ghcr.io/arkiv-network/arkiv-reth-dev:v1.0.1 \
    node --dev --dev.block-time 1s --http --http.addr 0.0.0.0 --http.api eth,net,web3,txpool --http.corsdomain '*' --datadir /data > /dev/null
  sleep 6
  # Bee dev mode buys a postage batch without any chain.
  curl -s -X POST http://127.0.0.1:1633/stamps/100000000/20 && echo
  echo "Ethereum http://127.0.0.1:8545 · Bee http://127.0.0.1:1633 · Arkiv http://127.0.0.1:8547"
  ;;
down)
  [ -f $RUN/geth.pid ] && kill "$(cat $RUN/geth.pid)" 2>/dev/null || true
  docker rm -f okupa-bee okupa-arkiv > /dev/null 2>&1 || true
  ;;
*) echo "usage: stack.sh up|down"; exit 2 ;;
esac
