#include "bktr.h"

#include "integrity.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace nx {

enum : uint64_t { NODE = 0x4000, MAX_SETS = (NODE - 0x10) / 8, CHUNK = 4ull << 20 };

// The entries of a tree, raw (entry_size bytes each), checked for the structure common to both kinds
static std::vector<std::vector<uint8_t>> read_tree(const Reader &t, const uint8_t bh[0x10], uint32_t entry_size, uint64_t *end,
                                                   const char *kind) {
    const std::string w = t.what() + " (" + kind + " table)";
    if (memcmp(bh, "BKTR", 4) != 0 || le32(bh + 4) != 1) corrupt(w);
    uint32_t total = le32(bh + 8);
    const uint64_t per_set = (NODE - 0x10) / entry_size;
    if (total == 0) corrupt(w + " (no entries)");
    uint64_t sets = (total + per_set - 1) / per_set;
    if (sets > MAX_SETS)  // (a two-level tree: not seen in games' patches)
        fail(t.what() + ": this update's patch tables are larger than supported", t.what() + ": as tabelas de patch deste update são maiores que o suportado");
    check_range(0, mul(sets + 1, NODE, w.c_str()), t.size(), w.c_str());
    uint8_t node[NODE];
    t.read(node, NODE, 0);
    if (le32(node + 4) != sets) corrupt(w + " (set count)");
    *end = le64(node + 8);
    std::vector<std::vector<uint8_t>> out;
    std::vector<uint8_t> set(NODE);
    for (uint64_t i = 0; i < sets; i++) {
        t.read(set.data(), NODE, (i + 1) * NODE);
        uint32_t count = le32(set.data() + 4);
        uint64_t set_end = le64(set.data() + 8);
        if (le32(set.data()) != i || count == 0 || count > per_set) corrupt(w + " (entry set)");
        uint64_t start = le64(set.data() + 0x10);
        if (start != le64(node + 0x10 + 8 * i)) corrupt(w + " (set start)");
        uint64_t next = i + 1 < sets ? le64(node + 0x10 + 8 * (i + 1)) : *end;
        if (set_end != next) corrupt(w + " (set end)");
        for (uint32_t k = 0; k < count; k++)
            out.emplace_back(set.begin() + 0x10 + k * entry_size, set.begin() + 0x10 + (k + 1) * entry_size);
    }
    if (out.size() != total) corrupt(w + " (entry count)");
    return out;
}

// Offsets (the first field of every entry) start at 0, strictly increase and stay below the end
template <class E> static void check_offsets(const std::vector<E> &v, uint64_t E::*field, uint64_t end, const std::string &w) {
    if (v.empty() || v[0].*field != 0) corrupt(w + " (first entry)");
    for (size_t i = 0; i < v.size(); i++)
        if ((i + 1 < v.size() ? v[i + 1].*field : end) <= v[i].*field) corrupt(w + " (entry order)");
}

std::vector<IndirectEntry> read_indirect_tree(const Reader &table, const uint8_t bh[0x10], uint64_t *end) {
    std::vector<IndirectEntry> out;
    for (auto &e : read_tree(table, bh, 0x14, end, "indirect")) {
        IndirectEntry x{le64(&e[0]), le64(&e[8]), le32(&e[16])};
        if (x.storage > 1) corrupt(table.what() + " (indirect storage index)");
        out.push_back(x);
    }
    check_offsets(out, &IndirectEntry::virt, *end, table.what() + " (indirect table)");
    return out;
}

std::vector<CtrExEntry> read_ctr_ex_tree(const Reader &table, const uint8_t bh[0x10], uint64_t *end) {
    std::vector<CtrExEntry> out;
    for (auto &e : read_tree(table, bh, 0x10, end, "AesCtrEx")) {
        if (e[8] > 1) corrupt(table.what() + " (AesCtrEx encryption value)");
        CtrExEntry x{le64(&e[0]), e[8] == 0, le32(&e[12])};
        if (x.offset % 16) corrupt(table.what() + " (AesCtrEx alignment)");
        out.push_back(x);
    }
    check_offsets(out, &CtrExEntry::offset, *end, table.what() + " (AesCtrEx table)");
    return out;
}

namespace {

// The update section's own data: each AesCtrEx range decrypted with its generation in the counter
class CtrExReader : public Reader {
public:
    CtrExReader(ReaderPtr enc, const Key128 &key, const uint8_t upper[8], uint64_t nca_offset, std::vector<CtrExEntry> entries,
                uint64_t size, std::string what)
        : Reader(std::move(what)), enc_(std::move(enc)), aes_(key), nca_offset_(nca_offset), entries_(std::move(entries)), size_(size) {
        memcpy(upper_, upper, 8);
    }
    uint64_t size() const override { return size_; }

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override {
        auto *out = static_cast<uint8_t *>(dst);
        std::vector<uint8_t> buf;
        while (n) {
            size_t i = size_t(std::upper_bound(entries_.begin(), entries_.end(), off,
                                               [](uint64_t o, const CtrExEntry &e) { return o < e.offset; }) - entries_.begin()) - 1;
            const CtrExEntry &e = entries_[i];
            uint64_t range_end = i + 1 < entries_.size() ? entries_[i + 1].offset : size_;
            uint64_t a = off & ~15ull, skip = off - a;
            uint64_t want = std::min<uint64_t>(n, std::min<uint64_t>(range_end - off, CHUNK));
            uint64_t span16 = (skip + want + 15) & ~15ull;
            buf.resize(size_t(span16));
            enc_->read(buf.data(), span16, a);  // (range ends are 16-aligned: the span stays in the range)
            if (e.encrypted) {
                uint8_t ctr[16], upper[8];
                memcpy(upper, upper_, 4);
                for (int k = 0; k < 4; k++) upper[4 + k] = uint8_t(e.generation >> (24 - 8 * k));
                make_counter(ctr, upper, nca_offset_ + a);
                aes_.crypt(buf.data(), size_t(span16), ctr);
            }
            memcpy(out, buf.data() + skip, size_t(want));
            out += want, off += want, n -= want;
        }
    }

private:
    ReaderPtr enc_;
    AesCtr aes_;
    uint8_t upper_[8];
    uint64_t nca_offset_;
    std::vector<CtrExEntry> entries_;
    uint64_t size_;
};

// The patched section: each range from the base section or from the update's data
class IndirectReader : public Reader {
public:
    IndirectReader(ReaderPtr base, ReaderPtr patch, std::vector<IndirectEntry> entries, uint64_t size, std::string what)
        : Reader(std::move(what)), src_{std::move(base), std::move(patch)}, entries_(std::move(entries)), size_(size) {}
    uint64_t size() const override { return size_; }

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override {
        auto *out = static_cast<uint8_t *>(dst);
        while (n) {
            size_t i = size_t(std::upper_bound(entries_.begin(), entries_.end(), off,
                                               [](uint64_t o, const IndirectEntry &e) { return o < e.virt; }) - entries_.begin()) - 1;
            const IndirectEntry &e = entries_[i];
            uint64_t range_end = i + 1 < entries_.size() ? entries_[i + 1].virt : size_;
            uint64_t k = std::min(n, range_end - off);
            src_[e.storage]->read(out, k, e.phys + (off - e.virt));
            out += k, off += k, n -= k;
        }
    }

public:
    // "the base game's RomFS section at 0x... (n bytes)" and / or "the update's own data at 0x...": which package's
    // file a failed block is in (copy that one again), and where in its section
    std::string origin(uint64_t off, uint64_t n, bool pt) const override {
        std::string out;
        while (n && off < size_) {
            size_t i = size_t(std::upper_bound(entries_.begin(), entries_.end(), off,
                                               [](uint64_t o, const IndirectEntry &e) { return o < e.virt; }) - entries_.begin()) - 1;
            const IndirectEntry &e = entries_[i];
            uint64_t range_end = i + 1 < entries_.size() ? entries_[i + 1].virt : size_;
            uint64_t k = std::min(n, range_end - off);
            char at[64];
            snprintf(at, sizeof at, "%#llx (%llu bytes)", (unsigned long long)(e.phys + (off - e.virt)), (unsigned long long)k);
            if (!out.empty()) out += pt ? " e " : " and ";
            out += e.storage ? (pt ? "dos dados do próprio update, em " : "the update's own data at ")
                             : (pt ? "da seção RomFS do jogo base, em " : "the base game's RomFS section at ");
            out += at;
            off += k, n -= k;
        }
        return out;
    }

private:
    ReaderPtr src_[2];
    std::vector<IndirectEntry> entries_;
    uint64_t size_;
};

}  // namespace

ReaderPtr open_patched_romfs(const Nca &patch, const Section &s, const ReaderPtr &base_section) {
    const std::string w = patch.what() + " RomFS patch";
    if (s.fs_type != FS_ROMFS || s.hash_type != HASH_IVFC || s.enc_type != ENC_CTR_EX)
        fail(w + ": the update's RomFS is not a patch (BKTR) section", w + ": o RomFS do update não é uma seção de patch (BKTR)");
    const uint8_t *pi = s.patch_info();
    uint64_t ind_off = le64(pi), ind_size = le64(pi + 8), ex_off = le64(pi + 0x20), ex_size = le64(pi + 0x28);
    check_range(ind_off, ind_size, s.size, (w + " (indirect table)").c_str());
    check_range(ex_off, ex_size, s.size, (w + " (AesCtrEx table)").c_str());
    // the tables: encrypted with the FS header's own counter (the raw section)
    ReaderPtr raw = patch.raw_section(s);
    uint64_t virt_size, data_size;
    auto indirect = read_indirect_tree(SubReader(raw, ind_off, ind_size, w), pi + 0x10, &virt_size);
    auto ctr_ex = read_ctr_ex_tree(SubReader(raw, ex_off, ex_size, w), pi + 0x30, &data_size);
    // the update's data ends where its tables begin, and every range points inside its storage
    if (data_size > ind_off || data_size > ex_off || data_size % 16) corrupt(w + " (AesCtrEx end)");
    for (size_t i = 0; i < indirect.size(); i++) {
        uint64_t len = (i + 1 < indirect.size() ? indirect[i + 1].virt : virt_size) - indirect[i].virt;
        uint64_t limit = indirect[i].storage ? data_size : base_section->size();
        if (indirect[i].phys > limit || len > limit - indirect[i].phys)
            fail(w + ": the update points outside the " + (indirect[i].storage ? "update's data" : "base game") +
                     " (an update for another base version?)",
                 w + ": o update aponta para fora " + (indirect[i].storage ? "dos dados do update" : "do jogo base") +
                     " (um update de outra versão base?)");
    }
    uint8_t upper[8];
    s.upper_counter(upper);
    auto enc = std::make_shared<SubReader>(patch.file(), s.offset, s.size, w);
    auto data = std::make_shared<CtrExReader>(enc, patch.section_key(), upper, s.offset, std::move(ctr_ex), data_size, w);
    auto virt = std::make_shared<IndirectReader>(base_section, data, std::move(indirect), virt_size, w);
    return std::make_shared<IvfcReader>(virt, s.hash_data(), patch.what() + " RomFS (patched)");
}

}  // namespace nx
