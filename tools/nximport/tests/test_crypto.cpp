// Known-answer tests from the standards (public test vectors; no Nintendo keys): the integration with mbedTLS, and
// Nintendo's big-endian XTS tweak.
#include "crypto.h"
#include "nca.h"
#include "test.h"

#include <cstring>
#include <string>

using namespace nx;

static std::vector<uint8_t> unhex(const std::string &s) {
    std::vector<uint8_t> v;
    for (size_t i = 0; i + 1 < s.size(); i += 2) v.push_back(uint8_t(std::stoi(s.substr(i, 2), nullptr, 16)));
    return v;
}
template <size_t N> static Secret<N> key(const std::string &s) {
    Secret<N> k;
    auto v = unhex(s);
    memcpy(k.data(), v.data(), N);
    k.set = true;
    return k;
}

TEST(aes_ecb_fips197) {  // FIPS-197 appendix C.1
    auto k = key<16>("000102030405060708090a0b0c0d0e0f");
    auto pt = unhex("00112233445566778899aabbccddeeff");
    uint8_t ct[16], back[16];
    aes_ecb_encrypt(k, pt.data(), ct, 16);
    CHECK(hex(ct, 16) == "69c4e0d86a7b0430d8cdb78070b4c55a");
    aes_ecb_decrypt(k, ct, back, 16);
    CHECK(!memcmp(back, pt.data(), 16));
}

TEST(aes_ctr_sp800_38a) {  // SP 800-38A F.5.1, CTR-AES128, all four blocks
    auto k = key<16>("2b7e151628aed2a6abf7158809cf4f3c");
    auto ctr = unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    auto buf = unhex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710");
    AesCtr(k).crypt(buf.data(), buf.size(), ctr.data());
    CHECK(hex(buf.data(), buf.size()) ==
          "874d6191b620e3261bef6864990db6ce9806f66b7970fdff8617187bb9fffdff5ae4df3edbd5d35e5b4f09020db03eab1e031dda2fbe03d1792170a0f3009cee");
}

TEST(aes_ctr_partial_and_offset) {  // a read starting mid-stream: the counter of block i is the first one + i
    auto k = key<16>("2b7e151628aed2a6abf7158809cf4f3c");
    uint8_t c0[16], c2[16];
    auto ctr = unhex("f0f1f2f3f4f5f6f7f8f9fafbfcfdfeff");
    memcpy(c0, ctr.data(), 16);
    memcpy(c2, ctr.data(), 16);
    // + 2 with carry over the low bytes
    for (int i = 15, carry = 2; i >= 0 && carry; i--) { int v = c2[i] + carry; c2[i] = uint8_t(v); carry = v >> 8; }
    std::vector<uint8_t> all(64, 0), tail(32, 0);
    AesCtr a(k);
    a.crypt(all.data(), 64, c0);
    a.crypt(tail.data(), 32, c2);
    CHECK(!memcmp(all.data() + 32, tail.data(), 32));
}

TEST(aes_xts_ieee1619) {  // IEEE 1619-2007 vectors 1 and 2 (XTS-AES-128)
    auto k1 = key<32>("0000000000000000000000000000000000000000000000000000000000000000");
    std::vector<uint8_t> b(32, 0);
    uint8_t du[16] = {};
    aes_xts_raw(k1, false, b.data(), 32, du);
    CHECK(hex(b.data(), 32) == "917cf69ebd68b2ec9b9fe9a3eadda692cd43d2f59598ed858c02c2652fbf922e");

    auto k2 = key<32>("1111111111111111111111111111111122222222222222222222222222222222");
    std::vector<uint8_t> p(32, 0x44);
    uint8_t du2[16] = {0x33, 0x33, 0x33, 0x33, 0x33};  // data unit 0x3333333333, little-endian
    aes_xts_raw(k2, false, p.data(), 32, du2);
    CHECK(hex(p.data(), 32) == "c454185e6a16936e39334038acef838bfb186fff7480adc4289382ecd6d394f0");
}

TEST(aes_xts_nintendo_tweak) {
    // Nintendo's tweak is the sector number big-endian: sector s equals raw XTS with that byte order
    auto k = key<32>("1111111111111111111111111111111122222222222222222222222222222222");
    std::vector<uint8_t> a(0x400), b(0x400);
    for (size_t i = 0; i < a.size(); i++) a[i] = b[i] = uint8_t(i * 7);
    aes_xts_nintendo(k, false, a.data(), 0x400, 0x0102, 0x200);
    uint8_t t0[16] = {}, t1[16] = {};
    t0[14] = 1, t0[15] = 2;
    t1[14] = 1, t1[15] = 3;
    aes_xts_raw(k, false, b.data(), 0x200, t0);
    aes_xts_raw(k, false, b.data() + 0x200, 0x200, t1);
    CHECK(a == b);
    aes_xts_nintendo(k, true, a.data(), 0x400, 0x0102, 0x200);
    for (size_t i = 0; i < a.size(); i++) CHECK(a[i] == uint8_t(i * 7));
}

TEST(sha256_kat) {  // FIPS 180-2 "abc" and the empty message
    uint8_t h[32];
    sha256("abc", 3, h);
    CHECK(hex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    sha256("", 0, h);
    CHECK(hex(h, 32) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Sha256 s;
    s.update("a", 1);
    s.update("bc", 2);
    s.finish(h);
    CHECK(hex(h, 32) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(nca_counter_layout) {
    uint8_t upper[8] = {1, 2, 3, 4, 5, 6, 7, 8}, c[16];
    make_counter(c, upper, 0x123456780ull);
    CHECK(hex(c, 16) == "01020304050607080000000012345678");
}
