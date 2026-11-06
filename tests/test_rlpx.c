#include "okupa_keccak.h"
#include "okupa_keys.h"
#include "okupa_rlpx.h"
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

/* EIP-8 test vectors (A initiates, B answers). */
static const char *KEY_A = "49a7b37aa6f6645917e7b807e9d1c00d4fa71f18343b0d4122a4d2df64dd6fee";
static const char *KEY_B = "b71c71a67e1177ad4e901695e1b4b9ee17ae16c6668d313eac2f96dbcda3f291";
static const char *EPH_A = "869d6ecf5211f1cc60418a13b9d870b22959d0c16f02bec714c960dd2298a32d";
static const char *EPH_B = "e238eb8e04fee6511ab04c6dd3c89ce097b11f25d584863ac2b6d5b35b1847e4";
static const char *NONCE_A = "7e968bba13b6c50e2c4cd7f241cc0d64d1ac25c7f5952df231ac6a2bda8ee5d6";
static const char *NONCE_B = "559aead08264d5795d3909718cdd05abd49572e84fe55590eef31a88a08fdffd";
static const char *AUTH2 = /* version 4 */
    "01b304ab7578555167be8154d5cc456f567d5ba302662433674222360f08d5f1534499d3678b513b"
    "0fca474f3a514b18e75683032eb63fccb16c156dc6eb2c0b1593f0d84ac74f6e475f1b8d56116b84"
    "9634a8c458705bf83a626ea0384d4d7341aae591fae42ce6bd5c850bfe0b999a694a49bbbaf3ef6c"
    "da61110601d3b4c02ab6c30437257a6e0117792631a4b47c1d52fc0f8f89caadeb7d02770bf999cc"
    "147d2df3b62e1ffb2c9d8c125a3984865356266bca11ce7d3a688663a51d82defaa8aad69da39ab6"
    "d5470e81ec5f2a7a47fb865ff7cca21516f9299a07b1bc63ba56c7a1a892112841ca44b6e0034dee"
    "70c9adabc15d76a54f443593fafdc3b27af8059703f88928e199cb122362a4b35f62386da7caad09"
    "c001edaeb5f8a06d2b26fb6cb93c52a9fca51853b68193916982358fe1e5369e249875bb8d0d0ec3"
    "6f917bc5e1eafd5896d46bd61ff23f1a863a8a8dcd54c7b109b771c8e61ec9c8908c733c0263440e"
    "2aa067241aaa433f0bb053c7b31a838504b148f570c0ad62837129e547678c5190341e4f1693956c"
    "3bf7678318e2d5b5340c9e488eefea198576344afbdf66db5f51204a6961a63ce072c8926c";
static const char *AUTH3 = /* version 56, three extra elements */
    "01b8044c6c312173685d1edd268aa95e1d495474c6959bcdd10067ba4c9013df9e40ff45f5bfd6f7"
    "2471f93a91b493f8e00abc4b80f682973de715d77ba3a005a242eb859f9a211d93a347fa64b597bf"
    "280a6b88e26299cf263b01b8dfdb712278464fd1c25840b995e84d367d743f66c0e54a586725b7bb"
    "f12acca27170ae3283c1073adda4b6d79f27656993aefccf16e0d0409fe07db2dc398a1b7e8ee93b"
    "cd181485fd332f381d6a050fba4c7641a5112ac1b0b61168d20f01b479e19adf7fdbfa0905f63352"
    "bfc7e23cf3357657455119d879c78d3cf8c8c06375f3f7d4861aa02a122467e069acaf513025ff19"
    "6641f6d2810ce493f51bee9c966b15c5043505350392b57645385a18c78f14669cc4d960446c1757"
    "1b7c5d725021babbcd786957f3d17089c084907bda22c2b2675b4378b114c601d858802a55345a15"
    "116bc61da4193996187ed70d16730e9ae6b3bb8787ebcaea1871d850997ddc08b4f4ea668fbf3740"
    "7ac044b55be0908ecb94d4ed172ece66fd31bfdadf2b97a8bc690163ee11f5b575a4b44e36e2bfb2"
    "f0fce91676fd64c7773bac6a003f481fddd0bae0a1f31aa27504e2a533af4cef3b623f4791b2cca6"
    "d490";
static const char *ACK2 = /* version 4 */
    "01ea0451958701280a56482929d3b0757da8f7fbe5286784beead59d95089c217c9b917788989470"
    "b0e330cc6e4fb383c0340ed85fab836ec9fb8a49672712aeabbdfd1e837c1ff4cace34311cd7f4de"
    "05d59279e3524ab26ef753a0095637ac88f2b499b9914b5f64e143eae548a1066e14cd2f4bd7f814"
    "c4652f11b254f8a2d0191e2f5546fae6055694aed14d906df79ad3b407d94692694e259191cde171"
    "ad542fc588fa2b7333313d82a9f887332f1dfc36cea03f831cb9a23fea05b33deb999e85489e645f"
    "6aab1872475d488d7bd6c7c120caf28dbfc5d6833888155ed69d34dbdc39c1f299be1057810f34fb"
    "e754d021bfca14dc989753d61c413d261934e1a9c67ee060a25eefb54e81a4d14baff922180c395d"
    "3f998d70f46f6b58306f969627ae364497e73fc27f6d17ae45a413d322cb8814276be6ddd13b885b"
    "201b943213656cde498fa0e9ddc8e0b8f8a53824fbd82254f3e2c17e8eaea009c38b4aa0a3f306e8"
    "797db43c25d68e86f262e564086f59a2fc60511c42abfb3057c247a8a8fe4fb3ccbadde17514b7ac"
    "8000cdb6a912778426260c47f38919a91f25f4b5ffb455d6aaaf150f7e5529c100ce62d6d92826a7"
    "1778d809bdf60232ae21ce8a437eca8223f45ac37f6487452ce626f549b3b5fdee26afd2072e4bc7"
    "5833c2464c805246155289f4";
static const char *ACK3 = /* version 57, three extra elements */
    "01f004076e58aae772bb101ab1a8e64e01ee96e64857ce82b1113817c6cdd52c09d26f7b90981cd7"
    "ae835aeac72e1573b8a0225dd56d157a010846d888dac7464baf53f2ad4e3d584531fa203658fab0"
    "3a06c9fd5e35737e417bc28c1cbf5e5dfc666de7090f69c3b29754725f84f75382891c561040ea1d"
    "dc0d8f381ed1b9d0d4ad2a0ec021421d847820d6fa0ba66eaf58175f1b235e851c7e2124069fbc20"
    "2888ddb3ac4d56bcbd1b9b7eab59e78f2e2d400905050f4a92dec1c4bdf797b3fc9b2f8e84a482f3"
    "d800386186712dae00d5c386ec9387a5e9c9a1aca5a573ca91082c7d68421f388e79127a5177d4f8"
    "590237364fd348c9611fa39f78dcdceee3f390f07991b7b47e1daa3ebcb6ccc9607811cb17ce51f1"
    "c8c2c5098dbdd28fca547b3f58c01a424ac05f869f49c6a34672ea2cbbc558428aa1fe48bbfd6115"
    "8b1b735a65d99f21e70dbc020bfdface9f724a0d1fb5895db971cc81aa7608baa0920abb0a565c9c"
    "436e2fd13323428296c86385f2384e408a31e104670df0791d93e743a3a5194ee6b076fb6323ca59"
    "3011b7348c16cf58f66b9633906ba54a2ee803187344b394f75dd2e663a57b956cb830dd7a908d4f"
    "39a2336a61ef9fda549180d4ccde21514d117b6c6fd07a9102b5efe710a32af4eeacae2cb3b1dec0"
    "35b9593b48b9d3ca4c13d245d5f04169b0b1";

static uint8_t ka[32], kb[32], pub_a[64], pub_b[64], eph_a[64], eph_b[64], nonce_a[32], nonce_b[32];

static void auth(const char *packet, uint64_t want_version)
{
    static uint8_t buf[OK_RLPX_MAX_PACKET];
    ok_rlpx_remote r;
    size_t n = hex(packet, buf, sizeof buf);
    assert(ok_rlpx_packet_len(buf) == n);
    assert(ok_rlpx_auth_read(kb, buf, n, &r) == OK_OK && r.version == want_version);
    assert(!memcmp(r.pub, pub_a, 64) && !memcmp(r.eph, eph_a, 64) && !memcmp(r.nonce, nonce_a, 32));
}

static void ack(const char *packet, uint64_t want_version)
{
    static uint8_t buf[OK_RLPX_MAX_PACKET];
    ok_rlpx_remote r;
    size_t n = hex(packet, buf, sizeof buf);
    assert(ok_rlpx_ack_read(ka, buf, n, &r) == OK_OK && r.version == want_version);
    assert(!memcmp(r.eph, eph_b, 64) && !memcmp(r.nonce, nonce_b, 32));
}

int main(void)
{
    static uint8_t buf[OK_RLPX_MAX_PACKET], copy[OK_RLPX_MAX_PACKET];
    uint8_t k[32];
    ok_rlpx_entropy e;
    ok_rlpx_remote r;
    size_t n;

    hex(KEY_A, ka, 32);
    hex(KEY_B, kb, 32);
    hex(NONCE_A, nonce_a, 32);
    hex(NONCE_B, nonce_b, 32);
    assert(ok_keys_public(ka, pub_a) == OK_OK && ok_keys_public(kb, pub_b) == OK_OK);
    hex(EPH_A, k, 32);
    assert(ok_keys_public(k, eph_a) == OK_OK);
    hex(EPH_B, k, 32);
    assert(ok_keys_public(k, eph_b) == OK_OK);

    auth(AUTH2, 4);
    auth(AUTH3, 56);
    ack(ACK2, 4);
    ack(ACK3, 57);

    /* What we write, the other side reads back: A's auth to B, B's ack to A. */
    hex(EPH_A, e.eph, 32);
    memcpy(e.nonce, nonce_a, 32);
    memset(e.ecies, 0x5a, 32);
    memset(e.iv, 0x3c, 16);
    assert(ok_rlpx_auth_write(ka, pub_b, &e, buf, sizeof buf, &n) == OK_OK && ok_rlpx_packet_len(buf) == n);
    memcpy(copy, buf, n);
    assert(ok_rlpx_auth_read(kb, buf, n, &r) == OK_OK && r.version == 4);
    assert(!memcmp(r.pub, pub_a, 64) && !memcmp(r.eph, eph_a, 64) && !memcmp(r.nonce, nonce_a, 32));
    /* Only B can open it, and any change is caught. */
    memcpy(buf, copy, n);
    assert(ok_rlpx_auth_read(ka, buf, n, &r) == OK_CRYPTO);
    memcpy(buf, copy, n);
    buf[100] ^= 1;
    assert(ok_rlpx_auth_read(kb, buf, n, &r) == OK_CRYPTO);
    memcpy(buf, copy, n);
    assert(ok_rlpx_auth_read(kb, buf, n - 1, &r) == OK_FORMAT);
    buf[1] ^= 1;
    assert(ok_rlpx_auth_read(kb, buf, n, &r) == OK_FORMAT);
    assert(ok_rlpx_auth_write(ka, pub_b, &e, buf, n - 1, &n) == OK_RANGE);

    hex(EPH_B, e.eph, 32);
    memcpy(e.nonce, nonce_b, 32);
    assert(ok_rlpx_ack_write(pub_a, &e, buf, sizeof buf, &n) == OK_OK);
    assert(ok_rlpx_ack_read(ka, buf, n, &r) == OK_OK && r.version == 4);
    assert(!memcmp(r.eph, eph_b, 64) && !memcmp(r.nonce, nonce_b, 32));
    /* An ack is not an auth. */
    assert(ok_rlpx_ack_write(pub_a, &e, buf, sizeof buf, &n) == OK_OK);
    assert(ok_rlpx_auth_read(ka, buf, n, &r) == OK_FORMAT);

    /* Session secrets, from B's side of (Auth2, Ack2), then A's side must mirror them. */
    {
        static uint8_t auth_wire[OK_RLPX_MAX_PACKET], ack_wire[OK_RLPX_MAX_PACKET];
        uint8_t aes[32], mac[32], want[32], ea[32], eb[32], d1[32], d2[32];
        ok_rlpx_remote from_a, from_b;
        ok_rlpx_session sa, sb;
        ok_keccak k1, k2;
        size_t auth_len, ack_len;

        hex(EPH_A, ea, 32);
        hex(EPH_B, eb, 32);
        auth_len = hex(AUTH2, auth_wire, sizeof auth_wire);
        ack_len = hex(ACK2, ack_wire, sizeof ack_wire);
        memcpy(buf, auth_wire, auth_len);
        assert(ok_rlpx_auth_read(kb, buf, auth_len, &from_a) == OK_OK);
        memcpy(buf, ack_wire, ack_len);
        assert(ok_rlpx_ack_read(ka, buf, ack_len, &from_b) == OK_OK);

        assert(ok_rlpx_derive(eb, from_a.eph, nonce_a, nonce_b, aes, mac) == OK_OK);
        hex("80e8632c05fed6fc2a13b0f8d31a3cf645366239170ea067065aba8e28bac487", want, 32);
        assert(!memcmp(aes, want, 32));
        hex("2ea74ec5dae199227dff1af715362700e989d889d7a493cb0639691efb8e5f98", want, 32);
        assert(!memcmp(mac, want, 32));

        assert(ok_rlpx_session_init(&sb, 0, eb, nonce_b, &from_a, auth_wire, auth_len, ack_wire, ack_len) == OK_OK);
        k1 = sb.ingress;
        ok_keccak_update(&k1, "foo", 3);
        ok_keccak_final(&k1, d1);
        hex("0c7ec6340062cc46f5e9f1e3cf86f8c8c403c5a0964f5df0ebd34a75ddc86db5", want, 32);
        assert(!memcmp(d1, want, 32));

        assert(ok_rlpx_session_init(&sa, 1, ea, nonce_a, &from_b, auth_wire, auth_len, ack_wire, ack_len) == OK_OK);
        k1 = sa.egress;
        k2 = sb.ingress;
        ok_keccak_final(&k1, d1);
        ok_keccak_final(&k2, d2);
        assert(!memcmp(d1, d2, 32));
        k1 = sa.ingress;
        k2 = sb.egress;
        ok_keccak_final(&k1, d1);
        ok_keccak_final(&k2, d2);
        assert(!memcmp(d1, d2, 32));
        k1 = sa.egress;
        k2 = sa.ingress;
        ok_keccak_final(&k1, d1);
        ok_keccak_final(&k2, d2);
        assert(memcmp(d1, d2, 32));
    }
    puts("rlpx: EIP-8 auth/ack vectors (v4 and future versions) read; our packets round-trip; session secrets match");
    return 0;
}
