#include "integrity.h"

#include "crypto.h"

#include <cstring>

namespace nx {

enum : uint64_t {
    MAX_HASH_TABLE = 64ull << 20,     // (HierarchicalSha256: 32 bytes per block; 64 MiB covers TBs)
    MAX_IVFC_LEVELS_MEM = 256ull << 20,
    CHUNK = 4ull << 20,
};

// A block's SHA-256 against the expected one. pad_to: the block size the hash was made over (a partial last block
// zero-padded), or 0 to hash the bytes as they are. Both forms are accepted for a partial last block: the hash says
// which bytes these are either way.
static bool block_ok(const uint8_t *data, size_t n, size_t pad_to, const uint8_t expect[32]) {
    uint8_t h[32];
    sha256(data, n, h);
    if (!memcmp(h, expect, 32)) return true;
    if (pad_to > n) {
        Sha256 s;
        s.update(data, n);
        static const uint8_t zero[4096] = {};
        for (size_t left = pad_to - n; left;) {
            size_t k = left < sizeof zero ? left : sizeof zero;
            s.update(zero, k);
            left -= k;
        }
        s.finish(h);
        return !memcmp(h, expect, 32);
    }
    return false;
}

[[noreturn]] static void mismatch(const std::string &what, uint64_t off) {
    std::string o = std::to_string(off);
    fail(what + ": integrity check failed at offset " + o +
             " (a wrong title key, a corrupt or incomplete file, or an update that doesn't belong to this base game)",
         what + ": falha na verificação de integridade no offset " + o +
             " (title key errada, arquivo corrompido ou incompleto, ou um update que não é deste jogo base)");
}

ReaderPtr open_sha256_region(const ReaderPtr &section, const uint8_t hd[0xF8], bool quick) {
    const std::string what = section->what();
    uint32_t block = le32(hd + 0x20), layers = le32(hd + 0x24);
    if (layers != 2 || block == 0 || block > (64u << 20)) corrupt(what + " (hash header)");
    uint64_t table_off = le64(hd + 0x28), table_size = le64(hd + 0x30);
    uint64_t data_off = le64(hd + 0x38), data_size = le64(hd + 0x40);
    check_range(table_off, table_size, section->size(), (what + " (hash table)").c_str());
    check_range(data_off, data_size, section->size(), (what + " (data)").c_str());
    if (table_size > MAX_HASH_TABLE || table_size % 32) corrupt(what + " (hash table)");
    std::vector<uint8_t> table = section->read_vec(table_size, table_off);
    uint8_t h[32];
    sha256(table.data(), table.size(), h);
    if (memcmp(h, hd, 32) != 0) mismatch(what, table_off);
    uint64_t blocks = (data_size + block - 1) / block;
    if (blocks * 32 > table_size) corrupt(what + " (hash table)");
    auto data = std::make_shared<SubReader>(section, data_off, data_size, what);
    if (quick) return data;
    std::vector<uint8_t> buf;
    for (uint64_t b = 0; b < blocks; b++) {
        uint64_t off = b * block, n = data_size - off < block ? data_size - off : block;
        buf.resize(n);
        data->read(buf.data(), n, off);
        if (!block_ok(buf.data(), n, 0, &table[b * 32])) mismatch(what, data_off + off);
    }
    return data;
}

// IVFC header (FS header + 0x08): "IVFC", version, master hash size, level count (7: master + 6), then 6 level infos
// of 0x18 (offset, size, log2 block size, reserved) at 0x10, a salt at 0xA0, the master hash at 0xC0.
struct Level {
    uint64_t off, size;
    uint32_t block;
};

static void parse_ivfc(const uint8_t hd[0xF8], uint64_t storage_size, const std::string &what, Level lv[6]) {
    if (memcmp(hd, "IVFC", 4) != 0 || le32(hd + 8) != 0x20 || le32(hd + 12) != 7) corrupt(what + " (IVFC header)");
    for (int i = 0; i < 6; i++) {
        const uint8_t *p = hd + 0x10 + 0x18 * i;
        uint32_t log2 = le32(p + 16);
        if (log2 < 4 || log2 > 24) corrupt(what + " (IVFC block size)");
        lv[i] = {le64(p), le64(p + 8), 1u << log2};
        check_range(lv[i].off, lv[i].size, storage_size, (what + " (IVFC level)").c_str());
    }
    for (int i = 1; i < 6; i++) {  // level i-1 holds a hash for each block of level i
        uint64_t blocks = (lv[i].size + lv[i].block - 1) / lv[i].block;
        if (mul(blocks, 32, what.c_str()) > lv[i - 1].size) corrupt(what + " (IVFC levels)");
    }
    if (lv[0].size > lv[0].block) corrupt(what + " (IVFC level 1)");  // (one block, under the one master hash)
}

// Level i read whole and checked against the hashes in `above` (32 bytes per block)
static std::vector<uint8_t> read_level(const Reader &storage, const Level &l, const uint8_t *above, const std::string &what) {
    std::vector<uint8_t> v = storage.read_vec(l.size, l.off);
    for (uint64_t off = 0, b = 0; off < l.size; off += l.block, b++) {
        uint64_t n = l.size - off < l.block ? l.size - off : l.block;
        if (!block_ok(v.data() + off, n, l.block, above + 32 * b)) mismatch(what, l.off + off);
    }
    return v;
}

bool IvfcReader::quick_check(const Reader &storage, const uint8_t hd[0xF8]) {
    Level lv[6];
    parse_ivfc(hd, storage.size(), storage.what(), lv);
    std::vector<uint8_t> l1 = storage.read_vec(lv[0].size, lv[0].off);
    return block_ok(l1.data(), l1.size(), lv[0].block, hd + 0xC0);
}

IvfcReader::IvfcReader(ReaderPtr storage, const uint8_t hd[0xF8], std::string what)
    : Reader(std::move(what)), storage_(std::move(storage)) {
    Level lv[6];
    parse_ivfc(hd, storage_->size(), this->what(), lv);
    uint64_t mem = 0;
    for (int i = 0; i < 5; i++) mem = add(mem, lv[i].size, this->what().c_str());
    if (mem > MAX_IVFC_LEVELS_MEM) corrupt(this->what() + " (IVFC levels too large)");
    std::vector<uint8_t> above(hd + 0xC0, hd + 0xE0);
    for (int i = 0; i < 5; i++) above = read_level(*storage_, lv[i], above.data(), this->what());
    hashes_ = std::move(above);
    data_off_ = lv[5].off, data_size_ = lv[5].size, block_size_ = lv[5].block;
}

void IvfcReader::read_impl(void *dst, uint64_t n, uint64_t off) const {
    auto *out = static_cast<uint8_t *>(dst);
    const uint64_t bs = block_size_;
    uint64_t b = off / bs, last = (off + n - 1) / bs;
    std::vector<uint8_t> buf;
    while (b <= last) {
        {  // (the block a previous read ended in: file boundaries inside one block)
            std::lock_guard<std::mutex> g(cache_lock_);
            if (b == cached_block_) {
                uint64_t start = b * bs, from = off > start ? off - start : 0;
                uint64_t to = off + n - start < bs ? off + n - start : bs;
                to = to < cache_.size() ? to : cache_.size();
                memcpy(out + (start + from - off), cache_.data() + from, size_t(to - from));
                b++;
                continue;
            }
        }
        uint64_t nb = last - b + 1, max_blocks = CHUNK / bs ? CHUNK / bs : 1;
        if (nb > max_blocks) nb = max_blocks;
        uint64_t start = b * bs, end = (b + nb) * bs < data_size_ ? (b + nb) * bs : data_size_;
        buf.resize(size_t(end - start));
        storage_->read(buf.data(), end - start, data_off_ + start);
        for (uint64_t i = 0; i < nb; i++) {
            uint64_t bo = i * bs, bn = (end - start) - bo < bs ? (end - start) - bo : bs;
            if (!block_ok(buf.data() + bo, bn, bs, &hashes_[(b + i) * 32])) mismatch(what(), data_off_ + start + bo);
        }
        uint64_t from = off > start ? off - start : 0, to = off + n - start < end - start ? off + n - start : end - start;
        memcpy(out + (start + from - off), buf.data() + from, size_t(to - from));
        {
            std::lock_guard<std::mutex> g(cache_lock_);
            uint64_t lb = b + nb - 1, lo = lb * bs - start;
            cache_.assign(buf.begin() + lo, buf.end());
            cached_block_ = lb;
        }
        b += nb;
    }
}

}  // namespace nx
