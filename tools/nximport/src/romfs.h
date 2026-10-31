// The RomFS image (an NCA's IVFC level 6): a 0x50-byte header, directory and file tables (entries linked by offset:
// parent, next sibling, first child dir, first child file), file data. Listed into paths, as hactool's --romfsdir
// writes them; the tables are checked as they're walked (bounds, names, cycles, duplicates).
#pragma once
#include "io.h"

#include <string>
#include <vector>

namespace nx {

struct RomfsFile {
    std::string path;  // "Pack/Dungeon000.pack"
    uint64_t offset;   // in the image
    uint64_t size;
};

struct RomfsListing {
    std::vector<std::string> dirs;  // every directory but the root, parents first ("Pack", "Pack/x")
    std::vector<RomfsFile> files;
    uint64_t total_bytes = 0;
};

RomfsListing list_romfs(const Reader &image);

}  // namespace nx
