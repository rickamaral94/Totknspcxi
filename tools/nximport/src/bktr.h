// BKTR: an update's RomFS section holds only what changed. Two bucket trees at the end of the section (their offsets
// and sizes in the FS header's patch info, 0x100) say how to rebuild the full section:
// - IndirectStorage: for each range of the patched (virtual) section, whether its bytes come from the base game's
//   RomFS section (storage 0) or from this section's own data (storage 1), and at which offset;
// - AesCtrExStorage: for each range of this section's data, the counter generation its AES-CTR used (it changes
//   between updates; the tables themselves use the FS header's).
// The virtual section is then read through the update's own IVFC tree (integrity.h): its hashes cover the patched
// bytes, which is what proves the rebuild right.
//
// Layout of a tree (single level, as games' patches have it): a 0x4000-byte node {u32 index, u32 count, u64 end
// offset, u64 start offset of each entry set}, then `count` entry sets of 0x4000 {u32 index, u32 count, u64 end
// offset, entries}. Indirect entry 0x14: virtual offset, physical offset, storage index. AesCtrEx entry 0x10: offset,
// u8 encryption (0 encrypted, 1 plain), 3 reserved, u32 generation.
#pragma once
#include "nca.h"

namespace nx {

struct IndirectEntry {
    uint64_t virt, phys;
    uint32_t storage;  // 0 base, 1 patch
};

struct CtrExEntry {
    uint64_t offset;
    bool encrypted;
    uint32_t generation;
};

// One tree's entries, checked: sorted, starting at 0, set headers and node offsets consistent, entry count as the
// FS header's; end: the tree's end offset. Throws on anything else.
std::vector<IndirectEntry> read_indirect_tree(const Reader &table, const uint8_t bucket_header[0x10], uint64_t *end);
std::vector<CtrExEntry> read_ctr_ex_tree(const Reader &table, const uint8_t bucket_header[0x10], uint64_t *end);

// The update's RomFS image (IVFC level 6 of the rebuilt section), every read checked. base_section: the base game's
// RomFS section, decrypted (Nca::raw_section); patch: the update's Program NCA and its RomFS section (AesCtrEx).
ReaderPtr open_patched_romfs(const Nca &patch, const Section &s, const ReaderPtr &base_section);

}  // namespace nx
