#include "okupa_discv4.h"
#include "okupa_keys.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t hex(const char *s, uint8_t *out, size_t cap)
{
    size_t i, n = strlen(s) / 2;
    assert(n <= cap);
    for (i = 0; i < n; i++) {
        unsigned v;
        assert(sscanf(s + 2 * i, "%2x", &v) == 1);
        out[i] = (uint8_t)v;
    }
    return n;
}

/* EIP-8: packets from future versions, with extra elements and trailing bytes,
 * signed by key b71c71a6... (the RLPx test key B). */
static const char PING4[] =
    "e9614ccfd9fc3e74360018522d30e1419a143407ffcce748de3e22116b7e8dc92ff74788c0b6663aaa3d67d641936511"
    "c8f8d6ad8698b820a7cf9e1be7155e9a241f556658c55428ec0563514365799a4be2be5a685a80971ddcfa80cb422cdd"
    "0101ec04cb847f000001820cfa8215a8d790000000000000000000000000000000018208ae820d058443b9a3550102";
static const char PING555[] =
    "577be4349c4dd26768081f58de4c6f375a7a22f3f7adda654d1428637412c3d7fe917cadc56d4e5e7ffae1dbe3efffb9"
    "849feb71b262de37977e7c7a44e677295680e9e38ab26bee2fcbae207fba3ff3d74069a50b902a82c9903ed37cc993c5"
    "0001f83e82022bd79020010db83c4d001500000000abcdef12820cfa8215a8d79020010db885a308d313198a2e037073"
    "488208ae82823a8443b9a355c5010203040531b9019afde696e582a78fa8d95ea13ce3297d4afb8ba6433e4154caa5ac"
    "6431af1b80ba76023fa4090c408f6b4bc3701562c031041d4702971d102c9ab7fa5eed4cd6bab8f7af956f7d565ee191"
    "7084a95398b6a21eac920fe3dd1345ec0a7ef39367ee69ddf092cbfe5b93e5e568ebc491983c09c76d922dc3";
static const char PONG[] =
    "09b2428d83348d27cdf7064ad9024f526cebc19e4958f0fdad87c15eb598dd61d08423e0bf66b2069869e1724125f820"
    "d851c136684082774f870e614d95a2855d000f05d1648b2d5945470bc187c2d2216fbe870f43ed0909009882e176a46b"
    "0102f846d79020010db885a308d313198a2e037073488208ae82823aa0fbc914b16819237dcd8801d7e53f69e9719ade"
    "cb3cc0e790c57e91ca4461c9548443b9a355c6010203c2040506a0c969a58f6f9095004c0177a6b47f451530cab38966"
    "a25cca5cb58f055542124e";
static const char FINDNODE[] =
    "c7c44041b9f7c7e41934417ebac9a8e1a4c6298f74553f2fcfdcae6ed6fe53163eb3d2b52e39fe91831b8a927bf4fc22"
    "2c3902202027e5e9eb812195f95d20061ef5cd31d502e47ecb61183f74a504fe04c51e73df81f25c4d506b26db451749"
    "0103f84eb840ca634cae0d49acb401d8a4c6b6fe8c55b70d115bf400769cc1400f3258cd31387574077f301b421bc84d"
    "f7266c44e9e6d569fc56be00812904767bf5ccd1fc7f8443b9a35582999983999999280dc62cc8255c73471e0a61da0c"
    "89acdc0e035e260add7fc0c04ad9ebf3919644c91cb247affc82b69bd2ca235c71eab8e49737c937a2c396";
static const char NEIGHBOURS[] =
    "c679fc8fe0b8b12f06577f2e802d34f6fa257e6137a995f6f4cbfc9ee50ed3710faf6e66f932c4c8d81d64343f429651"
    "328758b47d3dbc02c4042f0fff6946a50f4a49037a72bb550f3a7872363a83e1b9ee6469856c24eb4ef80b7535bcf99c"
    "0004f9015bf90150f84d846321163782115c82115db8403155e1427f85f10a5c9a7755877748041af1bcd8d474ec065e"
    "b33df57a97babf54bfd2103575fa829115d224c523596b401065a97f74010610fce76382c0bf32f84984010203040101"
    "b840312c55512422cf9b8a4097e9a6ad79402e87a15ae909a4bfefa22398f03d20951933beea1e4dfa6f968212385e82"
    "9f04c2d314fc2d4e255e0d3bc08792b069dbf8599020010db83c4d001500000000abcdef12820d05820d05b840386432"
    "00b172dcfef857492156971f0e6aa2c538d8b74010f8e140811d53b98c765dd2d96126051913f44582e8c199ad7c6d68"
    "19e9a56483f637feaac9448aacf8599020010db885a308d313198a2e037073488203e78203e8b8408dcab8618c3253b5"
    "58d459da53bd8fa68935a719aff8b811197101a4b2b47dd2d47295286fc00cc081bb542d760717d1bdd6bec2c37cd72e"
    "ca367d6dd3b9df738443b9a355010203b525a138aa34383fec3d2719a0";

/* The same packets from an independent encoder (eth-keys + pyrlp), key 0x11 * 32. */
static const char PY_PING[] = "b5bbaf17f25a8e073fc2ef2ee58dd98379610f2518c7c76f21c65c40a15337c80b8eb2bc1705e63ef547e3f0f25b2da593e76f16cf01b59c2190c26c93b4e04c235cce8416cef0e6cf622669d5e08d3e25d99648d6023f21ce6885f4bcf700420001dc04cb84c0a8000782765f82765fc9840102030482765d80846b49d214";
static const char PY_FINDNODE[] = "980b90d4f068f5ad44f6c65c120ccb31c6544809c3e87cac6cac77cda904748e41f874a49fda36c4e740e3c9c843ef7cb68455337daa43d1c3fb5dfe7db80dd0287b517c395c5369c78ed02cc89a0207c73ddf7e28eb5efa8ff37c1834eb01bd0003f847b840000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f846b49d214";
static const char PY_ENRREQ[] = "2e17f71c013b610faf38aec34905a19090e5ff60453a163fa83a56b2aaac2e71c6a287e2820aa2c43409349fda4d52a8a47fd804a666cded064aa8adb6c1733a224466090a334439169815b1027d66801682e6a7d8f1bcff5d39ba483407b8d10005c5846b49d214";
static const char PY_ENRRESP[] = "d67a2f892790e90f27c1f9127750d8ec20b2f31f3ec62393353ccc60f64bc4f369aa0eecceb2a96a0a4e6fd4b778938d7090e195e62adb36f2d2c4b9e82acb80699627374774c5d1906ecb7753a90fb930ab7f885c1181c2a5edf7be9f5f0aab0106f8c6a0aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaf8a3b840e473f078445bcb11cfaed875c3444be134d8292e89cb398aae0229656ea8f03b6e988d6cc78fc215e32c1a0c341a45e0cff47b89c0929bba2496cbfef7b9685486019ed270d38683657468c7c6846c1d942380826964827634826970849db46ae289736563703235366b31a102e2b4b288b74640d380d71caaa1db9ee6db463d325e403b8b221cb453354f71ce84736e6170c08374637082766083756470827660";

int main(void)
{
    static uint8_t buf[OK_V4_MAX], out[OK_V4_MAX], want[OK_V4_MAX];
    uint8_t key_b[32], pub_b[64], key[32], pub[64], h[32], target[64];
    ok_v4_endpoint from = {{192, 168, 0, 7}, 30303, 30303}, to = {{1, 2, 3, 4}, 30301, 0};
    ok_v4_packet p;
    ok_v4_node node;
    ok_enr enr;
    size_t n, len, pos = 0;
    int count = 0;

    hex("b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291", key_b, 32);
    assert(ok_keys_public(key_b, pub_b) == OK_OK);

    n = hex(PING4, buf, sizeof buf);
    assert(ok_v4_decode(buf, n, &p) == OK_OK && p.type == OK_V4_PING && !memcmp(p.sender, pub_b, 64));
    n = hex(PING555, buf, sizeof buf);
    assert(ok_v4_decode(buf, n, &p) == OK_OK && p.type == OK_V4_PING && !memcmp(p.sender, pub_b, 64));
    n = hex(PONG, buf, sizeof buf);
    assert(ok_v4_decode(buf, n, &p) == OK_OK && ok_v4_pong_read(&p, h) == OK_OK && h[0] == 0xfb && h[31] == 0x54);
    n = hex(FINDNODE, buf, sizeof buf);
    assert(ok_v4_decode(buf, n, &p) == OK_OK && p.type == OK_V4_FINDNODE);
    n = hex(NEIGHBOURS, buf, sizeof buf);
    assert(ok_v4_decode(buf, n, &p) == OK_OK && p.type == OK_V4_NEIGHBORS);
    while (ok_v4_neighbors_next(&p, &pos, &node) == OK_OK) {
        if (count == 0) assert(!memcmp(node.at.ip, "\x63\x21\x16\x37", 4) && node.at.udp == 4444 && node.at.tcp == 4445);
        count++;
    }
    assert(count == 2); /* the two IPv6 entries are skipped */
    buf[40] ^= 1;       /* any change breaks the hash */
    assert(ok_v4_decode(buf, n, &p) == OK_FORMAT);

    /* What we write is byte for byte what eth-keys writes (both sign with RFC 6979). */
    memset(key, 0x11, 32);
    assert(ok_keys_public(key, pub) == OK_OK);
    assert(ok_v4_ping(key, &from, &to, 1800000020u, out, sizeof out, &len, h) == OK_OK);
    n = hex(PY_PING, want, sizeof want);
    assert(len == n && !memcmp(out, want, n) && !memcmp(h, want, 32));
    for (n = 0; n < 64; n++) target[n] = (uint8_t)n;
    assert(ok_v4_findnode(key, target, 1800000020u, out, sizeof out, &len) == OK_OK);
    n = hex(PY_FINDNODE, want, sizeof want);
    assert(len == n && !memcmp(out, want, n));
    assert(ok_v4_enrrequest(key, 1800000020u, out, sizeof out, &len, h) == OK_OK);
    n = hex(PY_ENRREQ, want, sizeof want);
    assert(len == n && !memcmp(out, want, n));

    /* A Pong we write reads back, quoting the ping's hash. */
    assert(ok_v4_pong(key, &to, h, 1800000020u, out, sizeof out, &len) == OK_OK);
    assert(ok_v4_decode(out, len, &p) == OK_OK && !memcmp(p.sender, pub, 64));
    assert(ok_v4_pong_read(&p, target) == OK_OK && !memcmp(target, h, 32));

    /* An ENRResponse carrying a live Sepolia record: its signature checks out. */
    n = hex(PY_ENRRESP, buf, sizeof buf);
    assert(ok_v4_decode(buf, n, &p) == OK_OK && p.type == OK_V4_ENRRESPONSE);
    assert(ok_v4_enrresponse_read(&p, h, &enr) == OK_OK && h[0] == 0xaa);
    assert(enr.has_eth && !memcmp(enr.fork_hash, "\x6c\x1d\x94\x23", 4) && enr.tcp == 30304);
    assert(ok_v4_neighbors_next(&p, &pos, &node) == OK_FORMAT); /* wrong type */
    /* The shipped bootnode records all verify. */
    {
        static ok_v4_crawl c;
        uint8_t fork[4] = {0};
        int i;
        ok_v4_crawl_init(&c, key, fork, 1);
        for (i = 0; i < ok_mainnet_bootnode_count; i++) assert(ok_v4_crawl_add_enr(&c, ok_mainnet_bootnodes[i]) == OK_OK);
        for (i = 0; i < ok_sepolia_bootnode_count; i++) assert(ok_v4_crawl_add_enr(&c, ok_sepolia_bootnodes[i]) == OK_OK);
        assert(c.count == ok_mainnet_bootnode_count + ok_sepolia_bootnode_count);
    }
    puts("discv4: EIP-8 packets decode (future versions, trailing data); ours match eth-keys; ENR responses verify");
    return 0;
}
