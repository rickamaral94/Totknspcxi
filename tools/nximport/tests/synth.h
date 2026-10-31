// Synthetic packages for the tests: PFS0/HFS0/XCI, RomFS images, IVFC and SHA-256 hash trees, NCAs (NCA3 and
// NCA2), CNMTs and tickets, built with random keys made by the test. No Nintendo key or file is involved: the formats
// are laid out as docs/IMPORT-NSP-XCI.md describes, and the importer reads them back.
#pragma once
#include "crypto.h"
#include "nca.h"

#include <cstdint>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace synth {

using Bytes = std::vector<uint8_t>;
using Files = std::vector<std::pair<std::string, Bytes>>;

Bytes random_bytes(std::mt19937_64 &rng, size_t n);

struct Keys {
    nx::Key256 header_key;
    nx::Key128 kak[4];  // key_area_key_application_00..03
    nx::Key128 titlekek[4];
    explicit Keys(std::mt19937_64 &rng);
    std::string prod_keys() const;  // as a prod.keys file
};

Bytes pfs0(const Files &files);
Bytes hfs0(const Files &files);
Bytes xci(const Files &secure, bool key_area_prefix);

// A RomFS image of the files (paths with '/'), its data at 0x200 and its tables after it
Bytes romfs(const Files &files);

// An update's RomFS section (BKTR): the new section's ranges taken from the base section where some C-aligned
// chunk of it holds the same bytes (at any offset), else from the update's own data, split into AesCtrEx ranges of
// varied generations (every third one stored plain)
struct PatchSpec {
    Bytes base_plain;        // the base game's RomFS section, as nca() lays it out
    Bytes new_romfs;         // the update's RomFS image
    size_t chunk = 0x200;    // granularity of the indirect ranges
    bool split = false;      // one indirect entry per chunk (no merging): many entries, several entry sets
    size_t piece = 0x1230;   // AesCtrEx range size (rounded to 16)
};

struct SectionSpec {
    bool is_romfs;  // RomFS + IVFC, else PFS0 + HierarchicalSha256
    Bytes data;     // the PFS0 or the RomFS image
    const PatchSpec *patch = nullptr;  // an update's RomFS (BKTR) instead
};

// A section's plain bytes as nca() lays it out, and its FS header hash data
Bytes section_plain(const SectionSpec &s, uint32_t log2, uint8_t hash_data[0xF8]);

// A bucket tree (as bktr.h reads it): entries of entry_size bytes, each starting at offsets[i], ending at end
Bytes bucket_tree(const std::vector<Bytes> &entries, const std::vector<uint64_t> &offsets, uint64_t end, size_t entry_size);

struct NcaSpec {
    nx::ContentType type = nx::ContentType::Program;
    uint64_t title_id = 0x0100F2C0115B6000ull;
    uint8_t keygen = 0;  // the header's 0x220 (0x206 stays 0)
    std::vector<SectionSpec> sections;
    bool nca2 = false;
    bool rights = false;  // title key crypto: rights_id = title id + key generation
    uint32_t ivfc_block_log2 = 9;
};

struct Nca {
    Bytes bytes;
    nx::Key128 title_key;  // the plain title key, when spec.rights
    uint8_t rights_id[16];
};
Nca nca(const Keys &keys, const NcaSpec &spec, std::mt19937_64 &rng);

// "<id>.nca" for an NCA's bytes (id: the first 16 bytes of its SHA-256)
std::string nca_name(const Bytes &nca, bool meta);

struct CnmtRecord {
    Bytes nca;
    uint8_t type;  // nx::CnmtContentType
};
Bytes cnmt(uint64_t title_id, uint32_t version, uint8_t meta_type, uint64_t application_id, const std::vector<CnmtRecord> &records);

Bytes ticket(const uint8_t rights_id[16], const uint8_t enc_title_key[16], bool common);

// A whole game package: Program NCA (exefs + RomFS), Meta NCA, ticket when it uses a title key
struct GameSpec {
    uint64_t title_id = 0x0100F2C0115B6000ull;
    uint8_t meta_type = 0x80;
    uint32_t version = 0;
    Files exefs;
    Files romfs;
    bool rights = false;
    bool with_ticket = true;
    bool nca2 = false;
    uint8_t keygen = 0;
    // an update (meta_type 0x81): the base's RomFS it patches, and how
    const Files *base_romfs = nullptr;
    size_t patch_chunk = 0x200;
    bool patch_split = false;
};
struct Game {
    Files package;  // the NSP's files (or the XCI's secure partition)
    nx::Key128 title_key;
    uint8_t rights_id[16];
    Bytes program_nca;
};
Game game(const Keys &keys, const GameSpec &spec, std::mt19937_64 &rng);

// A small TOTK-like file set
Files sample_exefs(std::mt19937_64 &rng);
Files sample_romfs(std::mt19937_64 &rng);

}  // namespace synth
