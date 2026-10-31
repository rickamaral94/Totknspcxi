// mbedTLS configuration for nximport: the AES modes and the hash the Switch formats use, nothing else (no TLS, no
// PSA, no RNG). Hardware AES and SHA-256 where the CPU has them, picked at run time: the ARMv8 crypto extensions on
// the phone, AES-NI on an x86-64 host.
#pragma once

#define MBEDTLS_AES_C
#define MBEDTLS_CIPHER_MODE_CTR
#define MBEDTLS_CIPHER_MODE_XTS
#define MBEDTLS_SHA256_C

#if defined(__aarch64__)
#define MBEDTLS_AESCE_C
#define MBEDTLS_SHA256_USE_ARMV8_A_CRYPTO_IF_PRESENT
#elif defined(__x86_64__)
#define MBEDTLS_HAVE_ASM
#define MBEDTLS_AESNI_C
#endif
