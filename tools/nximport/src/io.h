// Random-access readers over the player's packages. Everything is read with pread at an offset: no file is mapped or
// read whole (a base game and its update are 15-20 GB). Readers are stacked: a file, a range of it (a PFS0 entry, an
// NCA section), a decrypting view (nca.h), the patched RomFS (bktr.h), a hash-verified view (integrity.h).
#pragma once
#include "error.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace nx {

class Reader {
public:
    virtual ~Reader() = default;
    virtual uint64_t size() const = 0;
    // Exactly n bytes at off, inside [0, size()); throws Error otherwise (a short read too)
    void read(void *dst, uint64_t n, uint64_t off) const {
        check_range(off, n, size(), what_.c_str());
        if (n) read_impl(dst, n, off);
    }
    std::vector<uint8_t> read_vec(uint64_t n, uint64_t off) const {
        std::vector<uint8_t> v(n);
        read(v.data(), n, off);
        return v;
    }
    // What this is, for messages ("base.nsp", "base.nsp/abc.nca section 1")
    const std::string &what() const { return what_; }
    // Where [off, off + n) really comes from, for a failure's message - an update's patched RomFS says which of its
    // bytes are the base game's and which the update's own (bktr.cpp); "" for a plain file. pt: in Portuguese.
    virtual std::string origin(uint64_t off, uint64_t n, bool pt) const { (void)off, (void)n, (void)pt; return {}; }

protected:
    explicit Reader(std::string what) : what_(std::move(what)) {}
    virtual void read_impl(void *dst, uint64_t n, uint64_t off) const = 0;

private:
    std::string what_;
};

using ReaderPtr = std::shared_ptr<const Reader>;

// A file descriptor (Android: the one SAF hands over, read in place). owns: closed with the reader.
class FdReader : public Reader {
public:
    FdReader(int fd, bool owns, std::string what);
    ~FdReader() override;
    uint64_t size() const override { return size_; }
    // Whether the descriptor allows reads at any offset (a content provider may hand over a pipe): checked before an
    // import starts, so a provider without it is said up front, not at 40% (docs/IMPORT-NSP-XCI.md, SAF)
    static bool seekable(int fd, uint64_t *size_out);

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override;

private:
    int fd_;
    bool owns_;
    uint64_t size_;
};

ReaderPtr open_file(const std::string &path);

// [base, base + size) of a parent reader
class SubReader : public Reader {
public:
    SubReader(ReaderPtr parent, uint64_t base, uint64_t size, std::string what);
    uint64_t size() const override { return size_; }

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override { parent_->read(dst, n, base_ + off); }

private:
    ReaderPtr parent_;
    uint64_t base_, size_;
};

// A reader over bytes in memory (tests, small decrypted tables; a keys file's text: secret, wiped when it goes)
class MemReader : public Reader {
public:
    MemReader(std::vector<uint8_t> data, std::string what, bool secret = false)
        : Reader(std::move(what)), data_(std::move(data)), secret_(secret) {}
    ~MemReader() override;
    uint64_t size() const override { return data_.size(); }
    // A descriptor's whole content (a keys file handed over by Android's file picker: maybe not seekable), at most
    // max bytes; the descriptor is closed
    static std::shared_ptr<MemReader> from_fd(int fd, size_t max, std::string what, bool secret);

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override;

private:
    std::vector<uint8_t> data_;
    bool secret_;
};

// Little-endian fields of headers read into memory
inline uint16_t le16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }
inline uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
inline uint64_t le64(const uint8_t *p) { return uint64_t(le32(p)) | uint64_t(le32(p + 4)) << 32; }

}  // namespace nx
