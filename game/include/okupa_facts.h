#ifndef OKUPA_FACTS_H
#define OKUPA_FACTS_H

#include <stdint.h>

/* Everything the console really did, for the technical screens (SELECT) and
 * the "how it works" walk-through. Only measured or received values go in
 * here: the platform fills it as things happen and the screens only read it.
 * A field left at zero / empty means "not happened yet", never "example". */

#define OK_FACTS_PEERS 8 /* the last dial attempts, newest first */
#define OK_FACTS_BLOCKS 4
#define OK_FACTS_LOG 32

typedef struct {
    uint8_t ip[4];
    uint16_t port;
    uint8_t pub[8];      /* first bytes of the node id */
    int stage;           /* OK_STAGE_* reached (okupa_session.h) */
    int disconnect;      /* devp2p reason (4 = too many peers), -1 none */
    char client[48];     /* from their Hello, e.g. "Geth/v1.16.7-stable/linux-amd64/go1.24" */
    uint32_t ms_tcp, ms_rlpx, ms_hello, ms_status; /* time to reach each step */
    uint64_t latest;     /* their head, from eth/69 Status */
    uint8_t latest_hash[32];
    uint8_t fork_hash[4];
    uint64_t fork_next;
} ok_fact_peer;

typedef struct {
    /* Ethereum: the network as this console sees it. */
    char network[12];
    uint64_t chain_id;
    uint8_t genesis[32];
    uint8_t fork_hash[4];
    uint64_t fork_next;

    /* Discovery. */
    char dns_tree[72];       /* enrtree root domain */
    uint64_t dns_seq;        /* sequence of the signed root */
    int dns_verified;        /* root signature checked */
    int dns_records, dns_nodes; /* TXT records fetched, nodes found */
    int v4_sent, v4_received, v4_nodes; /* discv4 packets and nodes in the table */
    int candidates, tried, reached_status, full;

    /* The last dials. */
    ok_fact_peer peers[OK_FACTS_PEERS];
    int peer_count;
    uint64_t best_head;      /* highest block any peer told us about */
    uint8_t best_head_hash[32];

    /* Traffic. */
    uint32_t bytes_out, bytes_in, frames_out, frames_in;

    /* The account. */
    char address[43];
    int scribble_samples;    /* stylus samples that made the key */
    uint64_t nonce;          /* next nonce, counted on the console */

    /* The last transaction signed here. */
    uint8_t tx[1200];
    uint32_t tx_len;
    uint8_t tx_hash[32];
    uint64_t tx_nonce, tx_gas, tx_max_fee, tx_tip; /* fees in wei */
    uint32_t tx_data_len;
    uint8_t tx_r[32], tx_s[32];
    int tx_v;
    uint32_t us_sign;        /* time to sign on this CPU */
    int tx_handed;           /* nodes it was handed to */
    char tx_kind[12];        /* "hello", "drawing" */

    /* Inclusion, checked by the console itself over P2P: a peer's header
     * whose transactions root commits to our transaction. Not finality. */
    int incl_state;          /* 0 not looked, 1 looking, 2 found, 3 root mismatch */
    uint64_t incl_block;
    uint8_t incl_block_hash[32];
    uint8_t incl_tx_root[32];
    uint32_t incl_index, incl_block_txs;
    int incl_root_ok, incl_hash_ok; /* trie root recomputed, header hash recomputed */
    uint64_t blocks_checked;

    /* 0G: the last decision asked of the mind. */
    char og_host[48], og_model[48], og_provider[43], og_signer[43];
    char og_tls[24];         /* e.g. "TLS 1.2 ECDHE-ECDSA-AES128-GCM" */
    uint8_t og_req_sha[32], og_resp_sha[32];
    uint8_t og_sig[65];
    int og_verified;         /* the TEE signature recovers to the provider's signer */
    char og_request_id[48];
    uint32_t og_ms;
    char og_decision[40];    /* the closed-set action it chose */
    char og_storage_root[67];
    char og_clock[32];       /* where UTC came from, e.g. "NTP, RTC 4d 2h slow" */
    char og_error[32];       /* why the last attempt stopped, "" if it did not */
    /* For the thought strip: the last verified decision as the okupa lived
     * it. og_state "decided" or "no answer" (a safe stop: nothing invented). */
    char og_state[12];
    char og_said[48];        /* its line, or "" if it said nothing */
    char og_thought[64];     /* what it thought, or "" */
    char og_feature[40];     /* what else it did this time: "heard: ...", "saw: ...", "guessed: ..." */
    uint32_t og_decisions;   /* verified decisions since the console started */
    uint32_t og_features;    /* what works here: 1 decide, 2 hear, 4 see, 8 guess, 16 dream, 32 store, 64 letters */

    /* The machine. */
    int dsi_mode;            /* 133 MHz DSi mode */
    uint32_t heap_used, heap_free, code_size;
    char checks[10][33];     /* the core's timed known-answer checks, run on this console */
    int checks_n;
    char build[24];
    char ssid[33];
    uint8_t ip[4];
    int rssi;

    /* What just happened, newest last: a live feed for the screens. */
    struct {
        uint32_t t;   /* seconds since the console started */
        char src;     /* 'E' Ethereum, 'G' 0G, 'D' the DSi itself */
        char text[40];
    } log[OK_FACTS_LOG];
    uint32_t log_count; /* entries ever written; the ring keeps the last OK_FACTS_LOG */
} ok_facts;

/* The one record, owned by the platform. */
extern ok_facts ok_facts_now;

/* Adds a line to the live feed. now is seconds since start (any clock that
 * only moves forward). */
void ok_facts_log(uint32_t now, char src, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

#endif
