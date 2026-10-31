// The ciphers and hash the formats use, over mbedTLS (third_party/mbedtls, Apache-2.0): AES-128 ECB (key derivation,
// key areas, title keys), CTR (NCA sections), XTS with Nintendo's tweak (NCA headers), SHA-256 (hash trees). Key
// material lives in Secret<N>, wiped when it goes away (mbedtls_platform_zeroize: not optimized out).
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace nx {

void wipe(void *p, size_t n);

template <size_t N> struct Secret {
    std::array<uint8_t, N> b{};
    bool set = false;
    Secret() = default;
    Secret(const Secret &o) : b(o.b), set(o.set) {}
    Secret &operator=(const Secret &o) { b = o.b, set = o.set; return *this; }
    ~Secret() { wipe(b.data(), N); }
    uint8_t *data() { return b.data(); }
    const uint8_t *data() const { return b.data(); }
};
using Key128 = Secret<16>;
using Key256 = Secret<32>;

// AES-128-ECB of one or more 16-byte blocks
void aes_ecb_decrypt(const Key128 &key, const uint8_t *in, uint8_t *out, size_t n);
void aes_ecb_encrypt(const Key128 &key, const uint8_t *in, uint8_t *out, size_t n);

// AES-128-CTR with a key set once; crypt() takes the 16-byte counter of the first block. (Thread-safe after
// construction: mbedTLS only reads the key schedule.)
class AesCtr {
public:
    explicit AesCtr(const Key128 &key);
    ~AesCtr();
    AesCtr(const AesCtr &) = delete;
    AesCtr &operator=(const AesCtr &) = delete;
    void crypt(uint8_t *buf, size_t n, const uint8_t counter[16]) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// AES-128-XTS over sectors of sector_size bytes, the first one numbered first_sector, the tweak being the sector
// number in big-endian (Nintendo's; IEEE 1619 puts it little-endian). n: a multiple of sector_size.
void aes_xts_nintendo(const Key256 &key, bool decrypt, uint8_t *buf, size_t n, uint64_t first_sector, size_t sector_size);

// Plain XTS with a caller's 16-byte data unit (the IEEE 1619 test vectors)
void aes_xts_raw(const Key256 &key, bool decrypt, uint8_t *buf, size_t n, const uint8_t data_unit[16]);

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256 &) = delete;
    Sha256 &operator=(const Sha256 &) = delete;
    void update(const void *p, size_t n);
    void finish(uint8_t out[32]);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
void sha256(const void *p, size_t n, uint8_t out[32]);

}  // namespace nx
