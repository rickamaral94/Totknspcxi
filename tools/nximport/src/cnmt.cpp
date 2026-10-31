#include "cnmt.h"

#include <cstring>

namespace nx {

std::string CnmtContent::file_name() const { return hex(id.data(), 16) + ".nca"; }

const CnmtContent *Cnmt::find(uint8_t t) const {
    const CnmtContent *found = nullptr;
    for (auto &c : contents) {
        if (c.type != t || c.id_offset != 0) continue;
        if (found) corrupt("CNMT (two contents of one type)");
        found = &c;
    }
    return found;
}

Cnmt parse_cnmt(const Reader &r) {
    const std::string w = r.what();
    if (r.size() < 0x20 || r.size() > (1u << 20)) corrupt(w);
    std::vector<uint8_t> b = r.read_vec(r.size(), 0);
    // header: title id, version, type, (platform), extended header size, content count, meta count, attributes, ...
    Cnmt c;
    c.title_id = le64(&b[0]);
    c.version = le32(&b[8]);
    c.type = b[0xC];
    uint16_t ext = le16(&b[0xE]), count = le16(&b[0x10]);
    uint64_t at = add(0x20, ext, w.c_str());
    check_range(at, uint64_t(count) * 0x38, b.size(), w.c_str());
    // extended header: an application's patch id / a patch's application id first
    c.application_id = c.type == META_PATCH && ext >= 8 ? le64(&b[0x20]) : c.title_id;
    for (uint16_t i = 0; i < count; i++, at += 0x38) {
        // content info: hash (0x20), content id (0x10), size (5 bytes; byte 5 attributes on newer firmware), type,
        // id offset
        CnmtContent e;
        memcpy(e.hash.data(), &b[at], 32);
        memcpy(e.id.data(), &b[at + 0x20], 16);
        e.size = 0;
        for (int k = 4; k >= 0; k--) e.size = e.size << 8 | b[at + 0x30 + k];
        e.type = b[at + 0x36];
        e.id_offset = b[at + 0x37];
        c.contents.push_back(e);
    }
    return c;
}

}  // namespace nx
