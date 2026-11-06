OKUPA ON A NINTENDO DSi
=======================

What you need
-------------
- A Nintendo DSi that runs homebrew (for example TWiLight Menu++), and its SD card.
- Optional: a Wi-Fi network saved in the console's own settings, so it can reach Ethereum.

Install
-------
1. Copy okupa.nds anywhere on the SD card (for example /roms/nds/).
2. Put the SD card back and start okupa.nds from your homebrew menu.

That is all. The game makes its own folder, sd:/okupa/, the first time it runs.

What it keeps in sd:/okupa/
---------------------------
account.key        The console's Ethereum key. It is made on the DSi from your
                   stylus scribbles (Dev -> Wallet) and never leaves the card.
                   It is plain text: keep the SD card private.
wifi.txt           Wi-Fi networks it has joined.
peers-mainnet.txt  Ethereum nodes that answered it, so it finds them faster.
nonce-mainnet.txt  How many transactions this console has signed.
pending-*.hex      A signed transaction waiting to be delivered.
look.txt           How your okupa looks.

Files you can add yourself (all optional)
-----------------------------------------
network.txt        Write "sepolia" in it to use the Sepolia testnet instead of
                   Ethereum mainnet.
letters-on.txt     An empty file. With it, the okupa may post letters on chain.
letters-to.txt     "0x<address> <name>": who the letters go to.

How it talks to Ethereum
------------------------
The DSi joins Ethereum's peer-to-peer network itself (devp2p), signs on the
console and hands its transactions to the nodes it meets. No server of ours
and no RPC sit in that path. It does not check inclusion itself yet: look up
your address on a block explorer.

Sending a hello or a drawing costs a little gas on mainnet. Send a few
thousandths of a euro in ETH to the address shown in Dev -> Wallet first.

Friends on the web
------------------
The web page runs a web okupa in your browser. Paste your DSi address in
"Visit" to see what your console signed, or give it a small drawing: it travels
inside one Ethereum transaction, in the format the DSi uses.
