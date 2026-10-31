// The player's keys: prod.keys ("name = hex", one per line) and title.keys ("rights id = title key"). Only what NCA
// decryption needs is kept; anything else in the file is skipped unread. Keys missing from the file are derived from
// master_key_XX and the *_source keys when those are there (as hactool does). Values never leave this module in a
// message or a log: errors name the key, never its bytes.
//
// title.keys holds title keys as they are in a common ticket's title key block: encrypted with titlekek_XX (XX: the
// content's key revision). A ticket inside the package wins over title.keys for the same rights id (nximport.cpp),
// and either is checked against the content before it's used.
#pragma once
#include "crypto.h"
#include "io.h"

#include <map>
#include <string>
#include <vector>

namespace nx {

enum { MAX_KEY_REV = 0x20 };

// Key area keys by the NCA header's key area index (0x207)
enum KakIndex { KAK_APPLICATION = 0, KAK_OCEAN = 1, KAK_SYSTEM = 2 };

class KeySet {
public:
    // prod.keys' text (wiped by the caller); throws on a malformed line of a key it uses (line number, name)
    void parse_prod_keys(const uint8_t *text, size_t n);
    // title.keys' text: rights id (32 hex) = encrypted title key (32 hex)
    void parse_title_keys(const uint8_t *text, size_t n);
    void derive();  // fill what's missing from master keys and sources

    const Key256 &header_key() const;
    const Key128 &key_area_key(int index, int rev) const;
    const Key128 &titlekek(int rev) const;
    // title.keys' encrypted title key for a rights id; nullptr when title.keys doesn't have it
    const Key128 *title_key(const uint8_t rights_id[16]) const;
    size_t title_key_count() const { return title_keys_.size(); }

private:
    Key256 header_key_, header_key_source_;
    Key128 kak_[3][MAX_KEY_REV], titlekek_[MAX_KEY_REV], master_key_[MAX_KEY_REV];
    Key128 kak_source_[3], titlekek_source_, header_kek_source_, aes_kek_generation_source_, aes_key_generation_source_;
    std::map<std::string, Key128> title_keys_;  // by rights id, lowercase hex
};

// "key_area_key_application_0b" (the name a missing key is reported by)
std::string key_name(const char *base, int rev);

// A file's whole text, for the parsers (prod.keys is a few KB); the caller wipes it
std::vector<uint8_t> read_small_file(const Reader &r, size_t max_size);

// Key revision of an NCA: the larger of its two key generation fields, minus one (0 and 1 both mean revision 0)
inline int key_revision(uint8_t keygen_old, uint8_t keygen) {
    int g = keygen_old > keygen ? keygen_old : keygen;
    return g ? g - 1 : 0;
}

}  // namespace nx
