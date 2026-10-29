#include "romfs.h"

#include <cstring>
#include <set>
#include <unordered_set>

namespace nx {

enum : uint64_t {
    EMPTY = 0xFFFFFFFFu,
    MAX_TABLE = 64ull << 20,  // (BOTW: ~40000 files, a few MB of tables)
    MAX_ENTRIES = 4u << 20,
    MAX_DEPTH = 64,
    MAX_NAME = 0x300,
};

static bool valid_component(const std::string &n) {
    if (n.empty() || n == "." || n == "..") return false;
    for (char c : n)
        if (c == '/' || c == '\\' || c == '\0') return false;
    return true;
}

RomfsListing list_romfs(const Reader &image) {
    const std::string w = image.what();
    if (image.size() < 0x50) corrupt(w);
    uint8_t h[0x50];
    image.read(h, sizeof h, 0);
    if (le64(h) != 0x50) corrupt(w + " (RomFS header)");
    uint64_t dir_off = le64(h + 0x18), dir_size = le64(h + 0x20);
    uint64_t file_off = le64(h + 0x38), file_size = le64(h + 0x40), data_off = le64(h + 0x48);
    if (dir_size > MAX_TABLE || file_size > MAX_TABLE) corrupt(w + " (RomFS tables too large)");
    check_range(dir_off, dir_size, image.size(), (w + " (RomFS directory table)").c_str());
    check_range(file_off, file_size, image.size(), (w + " (RomFS file table)").c_str());
    if (data_off > image.size()) corrupt(w + " (RomFS data)");
    std::vector<uint8_t> dirs = image.read_vec(dir_size, dir_off), files = image.read_vec(file_size, file_off);

    // an entry's name: its length at name_len_at, the name after the fixed part
    auto name_of = [&](const std::vector<uint8_t> &t, uint64_t off, uint64_t fixed, const char *what) {
        if (off > t.size() || fixed > t.size() - off) corrupt(w + what);
        uint32_t len = le32(&t[off + fixed - 4]);
        if (len > MAX_NAME || len > t.size() - off - fixed) corrupt(w + what);
        return std::string(reinterpret_cast<const char *>(&t[off + fixed]), len);
    };

    RomfsListing out;
    std::unordered_set<uint64_t> seen_dirs, seen_files;
    std::set<std::string> paths;
    struct Todo {
        uint64_t off;
        std::string path;
        int depth;
    };
    std::vector<Todo> stack{{0, "", 0}};
    if (dir_size < 0x18) corrupt(w + " (RomFS root)");
    seen_dirs.insert(0);
    while (!stack.empty()) {
        Todo d = stack.back();
        stack.pop_back();
        if (d.depth > int(MAX_DEPTH)) corrupt(w + " (RomFS too deep)");
        const uint8_t *e = &dirs[d.off];
        // directory entry: parent, sibling, first child dir, first child file, hash next, name length, name
        for (uint64_t f = le32(e + 12); f != EMPTY;) {
            if (!seen_files.insert(f).second || seen_files.size() > MAX_ENTRIES) corrupt(w + " (RomFS file table cycle)");
            // file entry: parent, sibling, data offset (8), data size (8), hash next, name length, name
            std::string name = name_of(files, f, 0x20, " (RomFS file entry)");
            if (!valid_component(name)) corrupt(w + " (RomFS file name)");
            const uint8_t *fe = &files[f];
            uint64_t off = add(data_off, le64(fe + 8), w.c_str()), size = le64(fe + 16);
            check_range(off, size, image.size(), (w + " (RomFS file data)").c_str());
            std::string path = d.path.empty() ? name : d.path + "/" + name;
            if (!paths.insert(path).second) corrupt(w + " (RomFS duplicate path)");
            out.files.push_back({path, off, size});
            out.total_bytes = add(out.total_bytes, size, w.c_str());
            f = le32(fe + 4);
        }
        std::vector<Todo> kids;
        for (uint64_t c = le32(e + 8); c != EMPTY;) {
            if (!seen_dirs.insert(c).second || seen_dirs.size() > MAX_ENTRIES) corrupt(w + " (RomFS directory table cycle)");
            std::string name = name_of(dirs, c, 0x18, " (RomFS directory entry)");
            if (!valid_component(name)) corrupt(w + " (RomFS directory name)");
            std::string path = d.path.empty() ? name : d.path + "/" + name;
            if (!paths.insert(path).second) corrupt(w + " (RomFS duplicate path)");
            out.dirs.push_back(path);
            kids.push_back({c, path, d.depth + 1});
            c = le32(&dirs[c] + 4);
        }
        stack.insert(stack.end(), kids.rbegin(), kids.rend());
    }
    return out;
}

}  // namespace nx
