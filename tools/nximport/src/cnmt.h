// Content metadata (a Meta NCA's "<Type>_<title id>.cnmt"): which title and version a package holds and the content
// ids of its NCAs. The import finds the game's Program NCA through it, never by taking the first Program it sees.
#pragma once
#include "io.h"

#include <array>
#include <string>
#include <vector>

namespace nx {

enum MetaType : uint8_t { META_APPLICATION = 0x80, META_PATCH = 0x81, META_ADDON = 0x82 };
enum CnmtContentType : uint8_t { CNT_META = 0, CNT_PROGRAM = 1, CNT_DATA = 2, CNT_CONTROL = 3 };

struct CnmtContent {
    std::array<uint8_t, 32> hash;  // SHA-256 of the whole NCA
    std::array<uint8_t, 16> id;    // its first 16 bytes; the NCA is "<id hex>.nca"
    uint64_t size;
    uint8_t type;
    uint8_t id_offset;
    std::string file_name() const;
};

struct Cnmt {
    uint64_t title_id;
    uint32_t version;
    uint8_t type;
    uint64_t application_id;  // a patch's application (its extended header); the title id itself for an application
    std::vector<CnmtContent> contents;
    const CnmtContent *find(uint8_t type) const;
};

Cnmt parse_cnmt(const Reader &r);

}  // namespace nx
