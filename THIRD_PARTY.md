# Third-party code

Every external piece is pinned, hashed and licensed here. Nothing is modified in place.

| Component | Path | Upstream | Version | License | SHA-256 |
|---|---|---|---|---|---|
| tiny_sha3 (Keccak-f[1600]) | `third_party/tiny_sha3/` | https://github.com/mjosaarinen/tiny_sha3 | `dcbb3192047c2a721f5f851db591871d428036a9` (2016-09-21) | MIT | `sha3.c` 0c08d2e5945048d7af5b910678a85908b4b6ec7d4cb05446d807b1389d71dba5, `sha3.h` d6801d7235145d2c0dfa8f1f2b88a41b62ca393f409fdf87b836638ec89ebb67 |
| libsecp256k1 (signatures, recovery, ECDH) | `third_party/secp256k1/` (git submodule) | https://github.com/bitcoin-core/secp256k1 | `v0.8.0` = `6e2c8bc4ecdc6e71dbe7a368f360d8d453ce435d` (2026-08-03) | MIT | pinned by the submodule commit |
| BearSSL (SHA-256, HMAC, constant-time AES) | `third_party/bearssl/` (git submodule) | https://www.bearssl.org/git/BearSSL | `v0.6` = `8ef7680081c61b486622f2d983c0d3d21e83caad` | MIT | pinned by the submodule commit |
