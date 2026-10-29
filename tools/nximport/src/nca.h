// NCA: Nintendo's content archive. The 0xC00-byte header is AES-XTS encrypted with header_key (0x200-byte sectors,
// big-endian tweak); it names the content (Program, Meta, ...), its title, its key generation, and up to four FS
// sections, each AES-CTR encrypted with a key from the header's key area (key_area_key_*_<rev>) or, when the content
// has a rights id, with its title key (from a ticket or title.keys, decrypted with titlekek_<rev>).
#pragma once
#include "container.h"
#include "keys.h"

#include <array>
#include <string>
#include <vector>

namespace nx {

enum class ContentType : uint8_t { Program = 0, Meta = 1, Control = 2, Manual = 3, Data = 4, PublicData = 5 };

enum FsType : uint8_t { FS_ROMFS = 0, FS_PFS0 = 1 };
enum HashType : uint8_t { HASH_AUTO = 0, HASH_NONE = 1, HASH_SHA256 = 2, HASH_IVFC = 3 };
enum EncType : uint8_t { ENC_AUTO = 0, ENC_NONE = 1, ENC_XTS = 2, ENC_CTR = 3, ENC_CTR_EX = 4 };

struct Section {
    int index;
    uint64_t offset, size;  // in the NCA
    std::array<uint8_t, 0x200> header;  // the FS header, decrypted and checked against the NCA header's hash of it
    uint8_t fs_type, hash_type, enc_type;
    const uint8_t *hash_data() const { return header.data() + 0x08; }
    const uint8_t *patch_info() const { return header.data() + 0x100; }
    // The counter's upper half (0x140: generation, secure value), as the counter's first 8 bytes
    void upper_counter(uint8_t out[8]) const;
};

class Nca {
public:
    // Opens and checks the header; tickets: the packages' common tickets (title key candidates before title.keys)
    Nca(ReaderPtr file, const KeySet &keys, const std::vector<Ticket> &tickets);

    ContentType content_type() const { return type_; }
    uint64_t title_id() const { return title_id_; }
    int key_revision() const { return rev_; }
    bool has_rights_id() const { return has_rights_; }
    const std::vector<Section> &sections() const { return sections_; }
    const Section *section(int index) const;
    const std::string &what() const { return file_->what(); }

    // A section decrypted, without its hashes checked (the base of a patched RomFS reads it raw: bktr.h)
    ReaderPtr raw_section(const Section &s) const;
    // A PFS0 section's files, its data checked against its hashes
    std::vector<Entry> open_pfs0(const Section &s) const;
    // A RomFS section's level 6 (the RomFS image), every read checked against the IVFC tree
    ReaderPtr open_romfs(const Section &s) const;

    // The section key (AES-CTR); for a patch's AesCtrEx section too
    const Key128 &section_key() const { return key_; }
    const ReaderPtr &file() const { return file_; }

private:
    void resolve_title_key(const KeySet &keys, const std::vector<Ticket> &tickets);
    bool key_works(const Key128 &key) const;

    ReaderPtr file_;
    ContentType type_;
    uint64_t title_id_;
    int rev_;
    bool has_rights_;
    uint8_t rights_id_[16];
    std::vector<Section> sections_;
    Key128 key_;
};

// The 16-byte counter of the block at an NCA offset: upper (8 bytes) + big-endian (offset >> 4)
inline void make_counter(uint8_t ctr[16], const uint8_t upper[8], uint64_t nca_offset) {
    for (int i = 0; i < 8; i++) ctr[i] = upper[i];
    uint64_t v = nca_offset >> 4;
    for (int i = 0; i < 8; i++) ctr[15 - i] = uint8_t(v >> (8 * i));
}

// A section's bytes decrypted with AES-CTR, the counter of each block from its offset in the NCA
class CtrReader : public Reader {
public:
    CtrReader(ReaderPtr encrypted, const Key128 &key, const uint8_t upper[8], uint64_t nca_offset, std::string what);
    uint64_t size() const override { return enc_->size(); }

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override;

private:
    ReaderPtr enc_;
    std::shared_ptr<AesCtr> aes_;
    uint8_t upper_[8];
    uint64_t nca_offset_;
};

}  // namespace nx
