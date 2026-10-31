// The packages: PFS0 (an NSP; an NCA's exefs), HFS0 (a game card's partitions) and the XCI around them. A package is
// a flat list of named files, each a reader over its bytes in the package (nothing copied).
#pragma once
#include "io.h"

#include <string>
#include <vector>

namespace nx {

struct Entry {
    std::string name;
    ReaderPtr data;
};

// PFS0 or HFS0 (magic picks); names checked: no '/', no "." or "..", no duplicates, no NUL
std::vector<Entry> parse_pfs0(const ReaderPtr &r);
std::vector<Entry> parse_hfs0(const ReaderPtr &r);

enum class PackageKind { NSP, XCI };

// An NSP's files, or the files of an XCI's secure partition (where its NCAs are; the game card's own keys aren't
// needed for them). Refuses compressed packages (.ncz entries: NSZ/XCZ) with a clear message.
std::vector<Entry> open_package(const ReaderPtr &file, PackageKind kind);

const Entry *find_entry(const std::vector<Entry> &entries, const std::string &name);

// A ticket's fields the import uses (only common tickets: personalized ones need the console's own RSA key)
struct Ticket {
    uint8_t rights_id[16];
    uint8_t title_key_block[16];  // encrypted with titlekek_<revision>
    uint8_t key_revision;
    bool common;
};
Ticket parse_ticket(const Reader &r);

}  // namespace nx
