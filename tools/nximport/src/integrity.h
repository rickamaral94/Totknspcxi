// The hash trees NCA sections carry, checked on everything the import reads: the proof that what was decrypted (and,
// for an update, rebuilt from base + patch: bktr.h) is byte for byte Nintendo's content. A wrong title key, a corrupt
// file or a patch applied to the wrong base fails here instead of producing a broken romfs.bin.
//
// HierarchicalSha256 (exefs, logo, meta): a master hash of a table of SHA-256s, one per block of the data.
// HierarchicalIntegrity / IVFC (RomFS): six levels, each the SHA-256s of the next one's blocks, level 1 under the
// master hash; level 6 is the data. Partial last blocks are hashed zero-padded to the block size.
#pragma once
#include "io.h"

#include <mutex>

namespace nx {

// The section's FS header hash data (0xF8 bytes at FS header + 0x08). Returns the data region, checked whole (exefs is
// small: ~60 MB at most) before it's returned. quick: only the master hash against the hash table (to tell whether a
// title key is right, without reading the data).
ReaderPtr open_sha256_region(const ReaderPtr &section, const uint8_t hash_data[0xF8], bool quick = false);

// IVFC: level 6 as a reader whose every read is checked block by block against level 5 (levels 1-5 checked when it's
// opened and kept in memory: 32 bytes per 16 KiB block of data, ~32 MiB for 16 GiB).
class IvfcReader : public Reader {
public:
    IvfcReader(ReaderPtr storage, const uint8_t hash_data[0xF8], std::string what);
    uint64_t size() const override { return data_size_; }
    // Whether level 1 matches the master hash (a right key) - no other level read
    static bool quick_check(const Reader &storage, const uint8_t hash_data[0xF8]);

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override;

private:
    ReaderPtr storage_;
    uint64_t data_off_, data_size_;
    uint32_t block_size_;
    std::vector<uint8_t> hashes_;  // level 5: one SHA-256 per level 6 block
    mutable std::mutex cache_lock_;
    mutable uint64_t cached_block_ = UINT64_MAX;
    mutable std::vector<uint8_t> cache_;
};

}  // namespace nx
