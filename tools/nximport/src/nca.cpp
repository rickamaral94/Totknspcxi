#include "nca.h"

#include "integrity.h"

#include <cstring>

namespace nx {

enum : uint64_t { MEDIA = 0x200, HEADER_SIZE = 0xC00, CTR_CHUNK = 4ull << 20 };

void Section::upper_counter(uint8_t out[8]) const {
    for (int i = 0; i < 8; i++) out[i] = header[0x147 - i];  // (stored little-endian; the counter is big-endian)
}

CtrReader::CtrReader(ReaderPtr encrypted, const Key128 &key, const uint8_t upper[8], uint64_t nca_offset, std::string what)
    : Reader(std::move(what)), enc_(std::move(encrypted)), aes_(std::make_shared<AesCtr>(key)), nca_offset_(nca_offset) {
    memcpy(upper_, upper, 8);
    if (enc_->size() % 16 || nca_offset % 16) corrupt(this->what() + " (section alignment)");
}

void CtrReader::read_impl(void *dst, uint64_t n, uint64_t off) const {
    auto *out = static_cast<uint8_t *>(dst);
    std::vector<uint8_t> buf;
    while (n) {
        uint64_t a = off & ~15ull, skip = off - a;
        uint64_t span = skip + n < CTR_CHUNK ? skip + n : CTR_CHUNK;
        uint64_t span16 = (span + 15) & ~15ull;  // (inside the section: its size is a multiple of 16)
        buf.resize(size_t(span16));
        enc_->read(buf.data(), span16, a);
        uint8_t ctr[16];
        make_counter(ctr, upper_, nca_offset_ + a);
        aes_->crypt(buf.data(), size_t(span16), ctr);
        uint64_t k = span - skip;
        memcpy(out, buf.data() + skip, size_t(k));
        out += k, off += k, n -= k;
    }
}

Nca::Nca(ReaderPtr file, const KeySet &keys, const std::vector<Ticket> &tickets) : file_(std::move(file)) {
    const std::string &w = file_->what();
    if (file_->size() < HEADER_SIZE) corrupt(w);
    std::vector<uint8_t> h = file_->read_vec(HEADER_SIZE, 0);
    const Key256 &hk = keys.header_key();
    aes_xts_nintendo(hk, true, h.data(), 0x400, 0, 0x200);
    bool nca3 = !memcmp(h.data() + 0x200, "NCA3", 4), nca2 = !memcmp(h.data() + 0x200, "NCA2", 4);
    if (!nca3 && !nca2)
        fail(w + ": the NCA header doesn't decrypt (a wrong header_key in prod.keys, or not an NCA)",
             w + ": o cabeçalho da NCA não descriptografa (header_key errada nas prod.keys, ou não é uma NCA)");
    // NCA3: the four FS headers continue the header's sectors (2-5); NCA2: each is its own sector 0
    if (nca3) aes_xts_nintendo(hk, true, h.data() + 0x400, 0x800, 2, 0x200);
    else
        for (int i = 0; i < 4; i++) aes_xts_nintendo(hk, true, h.data() + 0x400 + 0x200 * i, 0x200, 0, 0x200);

    type_ = ContentType(h[0x205]);
    if (h[0x205] > 5) corrupt(w + " (content type)");
    uint8_t kak_index = h[0x207];
    if (le64(h.data() + 0x208) != file_->size())
        fail(w + ": the file's size doesn't match its header (incomplete copy?)",
             w + ": o tamanho do arquivo não bate com o cabeçalho (cópia incompleta?)");
    title_id_ = le64(h.data() + 0x210);
    rev_ = nx::key_revision(h[0x206], h[0x220]);
    memcpy(rights_id_, h.data() + 0x230, 16);
    has_rights_ = false;
    for (uint8_t b : rights_id_) has_rights_ |= b != 0;

    for (int i = 0; i < 4; i++) {
        const uint8_t *e = h.data() + 0x240 + 0x10 * i;
        uint32_t start = le32(e), end = le32(e + 4);
        if (start == 0 && end == 0) continue;
        if (end <= start) corrupt(w + " (section table)");
        Section s;
        s.index = i;
        s.offset = uint64_t(start) * MEDIA;
        s.size = uint64_t(end - start) * MEDIA;
        check_range(s.offset, s.size, file_->size(), (w + " (section table)").c_str());
        memcpy(s.header.data(), h.data() + 0x400 + 0x200 * i, 0x200);
        uint8_t hash[32];
        sha256(s.header.data(), 0x200, hash);
        if (memcmp(hash, h.data() + 0x280 + 0x20 * i, 32) != 0) corrupt(w + " (FS header hash)");
        s.fs_type = s.header[2], s.hash_type = s.header[3], s.enc_type = s.header[4];
        // (sparse and compressed sections: a bucket tree header "BKTR" with entries in their info blocks)
        auto bucket_used = [&](size_t at) { return !memcmp(s.header.data() + at, "BKTR", 4) && le32(s.header.data() + at + 8) != 0; };
        if (bucket_used(0x148 + 0x10) || bucket_used(0x178 + 0x10))
            fail(w + ": sparse or compressed NCA sections are not supported", w + ": seções de NCA esparsas ou comprimidas não são suportadas");
        if (s.enc_type != ENC_NONE && s.enc_type != ENC_CTR && s.enc_type != ENC_CTR_EX)
            fail(w + ": section encryption type " + std::to_string(s.enc_type) + " is not supported",
                 w + ": o tipo de criptografia de seção " + std::to_string(s.enc_type) + " não é suportado");
        sections_.push_back(s);
    }
    if (has_rights_) {
        resolve_title_key(keys, tickets);
    } else {
        const Key128 &kak = keys.key_area_key(kak_index, rev_);
        uint8_t area[0x40];
        aes_ecb_decrypt(kak, h.data() + 0x300, area, 0x40);
        memcpy(key_.data(), area + 0x20, 16);  // (key 2: the AES-CTR key)
        key_.set = true;
        wipe(area, sizeof area);
    }
    wipe(h.data(), h.size());
}

const Section *Nca::section(int index) const {
    for (auto &s : sections_)
        if (s.index == index) return &s;
    return nullptr;
}

ReaderPtr Nca::raw_section(const Section &s) const {
    std::string w = what() + " section " + std::to_string(s.index);
    auto enc = std::make_shared<SubReader>(file_, s.offset, s.size, w);
    if (s.enc_type == ENC_NONE) return enc;
    uint8_t upper[8];
    s.upper_counter(upper);
    // (an AesCtrEx section read as plain CTR gives right bytes only where the generation is the header's: the patch's
    // tables. bktr.h reads its data with the right counters.)
    return std::make_shared<CtrReader>(enc, key_, upper, s.offset, w);
}

std::vector<Entry> Nca::open_pfs0(const Section &s) const {
    if (s.fs_type != FS_PFS0 || s.hash_type != HASH_SHA256) corrupt(what() + " (not a PFS0 section)");
    return parse_pfs0(open_sha256_region(raw_section(s), s.hash_data()));
}

ReaderPtr Nca::open_romfs(const Section &s) const {
    if (s.fs_type != FS_ROMFS || s.hash_type != HASH_IVFC || s.enc_type == ENC_CTR_EX) corrupt(what() + " (not a RomFS section)");
    return std::make_shared<IvfcReader>(raw_section(s), s.hash_data(), what() + " RomFS");
}

bool Nca::key_works(const Key128 &key) const {
    // the first section with a hash tree: its top level against the master hash (a wrong key gives noise)
    for (auto &s : sections_) {
        if (s.enc_type != ENC_CTR) continue;
        uint8_t upper[8];
        s.upper_counter(upper);
        auto r = std::make_shared<CtrReader>(std::make_shared<SubReader>(file_, s.offset, s.size, what()), key, upper, s.offset, what());
        try {
            if (s.hash_type == HASH_SHA256) return open_sha256_region(r, s.hash_data(), true), true;
            if (s.hash_type == HASH_IVFC) return IvfcReader::quick_check(*r, s.hash_data());
        } catch (const Error &) {
            return false;
        }
    }
    return true;  // (nothing to check it against: taken as it is)
}

void Nca::resolve_title_key(const KeySet &keys, const std::vector<Ticket> &tickets) {
    const Key128 &kek = keys.titlekek(rev_);
    std::vector<std::string> tried;
    auto attempt = [&](const uint8_t enc[16], const char *source) {
        Key128 k;
        aes_ecb_decrypt(kek, enc, k.data(), 16);
        k.set = true;
        if (key_works(k)) {
            key_ = k;
            return true;
        }
        tried.push_back(source);
        return false;
    };
    for (auto &t : tickets)
        if (t.common && !memcmp(t.rights_id, rights_id_, 16) && attempt(t.title_key_block, "ticket")) return;
    if (const Key128 *k = keys.title_key(rights_id_))
        if (attempt(k->data(), "title.keys")) return;
    std::string rid = hex(rights_id_, 8) + "...";  // (the title half: enough to tell which content)
    if (tried.empty())
        fail(what() + ": no title key for rights id " + rid + " (no common ticket in the package and none in title.keys)",
             what() + ": não há title key para o rights id " + rid + " (nenhum ticket common no pacote e nenhuma nas title.keys)");
    std::string list;
    for (auto &s : tried) list += (list.empty() ? "" : ", ") + s;
    fail(what() + ": the title key for rights id " + rid + " doesn't decrypt the content (tried: " + list + ")",
         what() + ": a title key do rights id " + rid + " não descriptografa o conteúdo (tentadas: " + list + ")");
}

}  // namespace nx
