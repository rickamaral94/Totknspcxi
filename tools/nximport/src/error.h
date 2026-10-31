// nximport errors: every failure the player can see is said in English and in Brazilian Portuguese (the app shows the
// one of its language). A message never holds a key's value: names of keys only (keys.cpp).
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace nx {

class Error : public std::runtime_error {
public:
    Error(std::string en, std::string pt) : std::runtime_error(en), en_(std::move(en)), pt_(std::move(pt)) {}
    const std::string &en() const { return en_; }
    const std::string &pt() const { return pt_; }

private:
    std::string en_, pt_;
};

// The import was cancelled (Importer::cancel): not a failure to report
class Cancelled : public Error {
public:
    Cancelled() : Error("Cancelled", "Cancelado") {}
};

// An NCA whose SHA-256 differs from its package's metadata (Importer::verify_ncas): the one failure that says the
// file's bytes changed - the app's "damaged" message is for this alone, not for a package it couldn't open
class HashMismatch : public Error {
public:
    HashMismatch(std::string en, std::string pt) : Error(std::move(en), std::move(pt)) {}
};

[[noreturn]] inline void fail(std::string en, std::string pt) { throw Error(std::move(en), std::move(pt)); }

// A file that isn't what it says (truncated, corrupt, out of bounds): one message shape for all the parsers
[[noreturn]] inline void corrupt(const std::string &what) {
    fail(what + ": the file is corrupt or truncated", what + ": o arquivo está corrompido ou incompleto");
}

// Checked arithmetic for offsets and sizes read from files
inline uint64_t add(uint64_t a, uint64_t b, const char *what) {
    uint64_t r;
    if (__builtin_add_overflow(a, b, &r)) corrupt(what);
    return r;
}
inline uint64_t mul(uint64_t a, uint64_t b, const char *what) {
    uint64_t r;
    if (__builtin_mul_overflow(a, b, &r)) corrupt(what);
    return r;
}
// [off, off + n) inside [0, size)
inline void check_range(uint64_t off, uint64_t n, uint64_t size, const char *what) {
    if (off > size || n > size - off) corrupt(what);
}

std::string hex(const uint8_t *p, size_t n);

}  // namespace nx
