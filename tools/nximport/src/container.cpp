#include "container.h"

#include <cstring>
#include <set>

namespace nx {

enum : uint32_t {
    MAX_ENTRIES = 0x10000,         // (an NSP holds a handful of NCAs; a game's exefs 8 modules)
    MAX_STRING_TABLE = 16u << 20,
};

bool valid_name(const std::string &n) {
    return !n.empty() && n != "." && n != ".." && n.find('/') == std::string::npos && n.find('\\') == std::string::npos &&
           n.find('\0') == std::string::npos;
}

// PFS0: "PFS0", count, string table size, reserved; entries of 0x18 (offset, size, name offset, reserved).
// HFS0: the same with entries of 0x40 (+ hashed size and a SHA-256 of the entry's first bytes).
static std::vector<Entry> parse_partition(const ReaderPtr &r, const char *magic, uint32_t entry_size) {
    const std::string what = r->what() + " (" + magic + ")";
    uint8_t h[16];
    if (r->size() < 16) corrupt(what);
    r->read(h, 16, 0);
    if (memcmp(h, magic, 4) != 0)
        fail(r->what() + " is not a " + magic + " archive", r->what() + " não é um arquivo " + magic);
    uint32_t count = le32(h + 4), strsize = le32(h + 8);
    if (count > MAX_ENTRIES || strsize > MAX_STRING_TABLE) corrupt(what);
    uint64_t table = uint64_t(count) * entry_size, header = 16 + table + strsize;
    check_range(0, header, r->size(), what.c_str());
    std::vector<uint8_t> t = r->read_vec(table + strsize, 16);
    const char *strings = reinterpret_cast<const char *>(t.data() + table);
    std::vector<Entry> out;
    std::set<std::string> seen;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = t.data() + uint64_t(i) * entry_size;
        uint64_t off = le64(e), size = le64(e + 8);
        uint32_t name_off = le32(e + 16);
        if (name_off >= strsize) corrupt(what);
        size_t len = strnlen(strings + name_off, strsize - name_off);
        if (name_off + len >= strsize) corrupt(what);  // (no terminating NUL inside the table)
        std::string name(strings + name_off, len);
        if (!valid_name(name) || !seen.insert(name).second) corrupt(what + " entry name");
        uint64_t abs = add(header, off, what.c_str());
        out.push_back({name, std::make_shared<SubReader>(r, abs, size, r->what() + "/" + name)});
    }
    return out;
}

std::vector<Entry> parse_pfs0(const ReaderPtr &r) { return parse_partition(r, "PFS0", 0x18); }
std::vector<Entry> parse_hfs0(const ReaderPtr &r) { return parse_partition(r, "HFS0", 0x40); }

const Entry *find_entry(const std::vector<Entry> &entries, const std::string &name) {
    for (auto &e : entries)
        if (e.name == name) return &e;
    return nullptr;
}

static bool ends_with(const std::string &s, const char *suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

std::vector<Entry> open_package(const ReaderPtr &file, PackageKind kind) {
    std::vector<Entry> files;
    if (kind == PackageKind::NSP) {
        files = parse_pfs0(file);
    } else {
        // XCI: the card header (magic "HEAD" at 0x100) points at the root HFS0 (0x130 offset, 0x138 size). A dump that
        // keeps the card's key area in front has it 0x1000 further.
        uint8_t m[4];
        uint64_t base = 0;
        if (file->size() >= 0x1200) {
            file->read(m, 4, 0x1100);
            if (!memcmp(m, "HEAD", 4)) base = 0x1000;
        }
        if (file->size() < base + 0x200) corrupt(file->what());
        uint8_t h[0x200];
        file->read(h, sizeof h, base);
        if (memcmp(h + 0x100, "HEAD", 4) != 0)
            fail(file->what() + " is not an XCI (no card header)", file->what() + " não é um XCI (sem cabeçalho de cartucho)");
        uint64_t root_off = add(base, le64(h + 0x130), "XCI root partition"), root_size = le64(h + 0x138);
        // (the root's header size; its partitions' data follow it, inside the file)
        check_range(root_off, root_size, file->size(), "XCI root partition");
        auto root = std::make_shared<SubReader>(file, root_off, file->size() - root_off, file->what());
        auto parts = parse_hfs0(root);
        const Entry *secure = find_entry(parts, "secure");
        if (!secure)
            fail(file->what() + ": the card has no secure partition", file->what() + ": o cartucho não tem a partição secure");
        files = parse_hfs0(secure->data);
    }
    for (auto &e : files)
        if (ends_with(e.name, ".ncz"))
            fail(file->what() + " is compressed (NSZ/XCZ): not supported yet. Use the uncompressed NSP or XCI.",
                 file->what() + " está comprimido (NSZ/XCZ): ainda não suportado. Use o NSP ou XCI sem compressão.");
    return files;
}

Ticket parse_ticket(const Reader &r) {
    // signature type -> signature size + padding to 0x40 (RSA-4096, RSA-2048, ECDSA; SHA-1 and SHA-256 kinds)
    uint8_t st[4];
    r.read(st, 4, 0);
    uint32_t type = le32(st);
    uint64_t sig;
    switch (type) {
    case 0x010000: case 0x010003: sig = 0x200 + 0x3C; break;
    case 0x010001: case 0x010004: sig = 0x100 + 0x3C; break;
    case 0x010002: case 0x010005: sig = 0x3C + 0x40; break;
    default: corrupt(r.what() + " (ticket signature type)");
    }
    uint64_t body = 4 + sig;
    uint8_t b[0x180];
    r.read(b, sizeof b, body);
    Ticket t;
    memcpy(t.title_key_block, b + 0x40, 16);
    t.common = b[0x141] == 0;
    t.key_revision = b[0x145];
    memcpy(t.rights_id, b + 0x160, 16);
    return t;
}

}  // namespace nx
