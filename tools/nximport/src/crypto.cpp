#include "crypto.h"

#include "error.h"

#include <cstring>
#include <mbedtls/aes.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

namespace nx {

void wipe(void *p, size_t n) { mbedtls_platform_zeroize(p, n); }

static void ecb(const Key128 &key, bool decrypt, const uint8_t *in, uint8_t *out, size_t n) {
    mbedtls_aes_context c;
    mbedtls_aes_init(&c);
    int r = decrypt ? mbedtls_aes_setkey_dec(&c, key.data(), 128) : mbedtls_aes_setkey_enc(&c, key.data(), 128);
    for (size_t i = 0; r == 0 && i + 16 <= n; i += 16)
        r = mbedtls_aes_crypt_ecb(&c, decrypt ? MBEDTLS_AES_DECRYPT : MBEDTLS_AES_ENCRYPT, in + i, out + i);
    mbedtls_aes_free(&c);
    if (r != 0 || n % 16) fail("AES-ECB failed", "AES-ECB falhou");
}
void aes_ecb_decrypt(const Key128 &key, const uint8_t *in, uint8_t *out, size_t n) { ecb(key, true, in, out, n); }
void aes_ecb_encrypt(const Key128 &key, const uint8_t *in, uint8_t *out, size_t n) { ecb(key, false, in, out, n); }

struct AesCtr::Impl {
    mbedtls_aes_context c;
};

AesCtr::AesCtr(const Key128 &key) : impl_(new Impl) {
    mbedtls_aes_init(&impl_->c);
    if (mbedtls_aes_setkey_enc(&impl_->c, key.data(), 128) != 0) fail("AES key setup failed", "Falha ao preparar a chave AES");
}

AesCtr::~AesCtr() { mbedtls_aes_free(&impl_->c); }

void AesCtr::crypt(uint8_t *buf, size_t n, const uint8_t counter[16]) const {
    uint8_t nonce[16], stream[16];
    size_t nc_off = 0;
    memcpy(nonce, counter, 16);
    if (mbedtls_aes_crypt_ctr(&impl_->c, n, &nc_off, nonce, stream, buf, buf) != 0) fail("AES-CTR failed", "AES-CTR falhou");
    wipe(stream, sizeof stream);
}

void aes_xts_raw(const Key256 &key, bool decrypt, uint8_t *buf, size_t n, const uint8_t data_unit[16]) {
    mbedtls_aes_xts_context c;
    mbedtls_aes_xts_init(&c);
    int r = decrypt ? mbedtls_aes_xts_setkey_dec(&c, key.data(), 256) : mbedtls_aes_xts_setkey_enc(&c, key.data(), 256);
    if (r == 0) r = mbedtls_aes_crypt_xts(&c, decrypt ? MBEDTLS_AES_DECRYPT : MBEDTLS_AES_ENCRYPT, n, data_unit, buf, buf);
    mbedtls_aes_xts_free(&c);
    if (r != 0) fail("AES-XTS failed", "AES-XTS falhou");
}

void aes_xts_nintendo(const Key256 &key, bool decrypt, uint8_t *buf, size_t n, uint64_t first_sector, size_t sector_size) {
    if (!sector_size || n % sector_size) fail("AES-XTS: not whole sectors", "AES-XTS: setores incompletos");
    mbedtls_aes_xts_context c;
    mbedtls_aes_xts_init(&c);
    int r = decrypt ? mbedtls_aes_xts_setkey_dec(&c, key.data(), 256) : mbedtls_aes_xts_setkey_enc(&c, key.data(), 256);
    for (size_t i = 0; r == 0 && i < n; i += sector_size) {
        uint8_t tweak[16] = {};
        uint64_t s = first_sector + i / sector_size;
        for (int j = 0; j < 8; j++) tweak[15 - j] = uint8_t(s >> (8 * j));
        r = mbedtls_aes_crypt_xts(&c, decrypt ? MBEDTLS_AES_DECRYPT : MBEDTLS_AES_ENCRYPT, sector_size, tweak, buf + i, buf + i);
    }
    mbedtls_aes_xts_free(&c);
    if (r != 0) fail("AES-XTS failed", "AES-XTS falhou");
}

struct Sha256::Impl {
    mbedtls_sha256_context c;
};

Sha256::Sha256() : impl_(new Impl) {
    mbedtls_sha256_init(&impl_->c);
    mbedtls_sha256_starts(&impl_->c, 0);
}
Sha256::~Sha256() { mbedtls_sha256_free(&impl_->c); }
void Sha256::update(const void *p, size_t n) { mbedtls_sha256_update(&impl_->c, static_cast<const uint8_t *>(p), n); }
void Sha256::finish(uint8_t out[32]) { mbedtls_sha256_finish(&impl_->c, out); }

void sha256(const void *p, size_t n, uint8_t out[32]) { mbedtls_sha256(static_cast<const uint8_t *>(p), n, out, 0); }

}  // namespace nx
