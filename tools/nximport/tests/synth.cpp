#include "synth.h"

#include "cnmt.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace synth {

static void put16(Bytes &b, size_t at, uint16_t v) { b[at] = uint8_t(v), b[at + 1] = uint8_t(v >> 8); }
static void put32(Bytes &b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; i++) b[at + i] = uint8_t(v >> (8 * i));
}
static void put64(Bytes &b, size_t at, uint64_t v) {
    for (int i = 0; i < 8; i++) b[at + i] = uint8_t(v >> (8 * i));
}
static size_t align(size_t v, size_t a) { return (v + a - 1) / a * a; }

Bytes random_bytes(std::mt19937_64 &rng, size_t n) {
    Bytes b(n);
    for (auto &c : b) c = uint8_t(rng());
    return b;
}

template <size_t N> static void random_key(nx::Secret<N> &k, std::mt19937_64 &rng) {
    for (auto &c : k.b) c = uint8_t(rng());
    k.set = true;
}

Keys::Keys(std::mt19937_64 &rng) {
    random_key(header_key, rng);
    for (int i = 0; i < 4; i++) random_key(kak[i], rng), random_key(titlekek[i], rng);
}

std::string Keys::prod_keys() const {
    std::string s = "; test keys made by the test\nheader_key = " + nx::hex(header_key.data(), 32) + "\n";
    for (int i = 0; i < 4; i++) {
        s += nx::key_name("key_area_key_application", i) + " = " + nx::hex(kak[i].data(), 16) + "\n";
        s += nx::key_name("titlekek", i) + " = " + nx::hex(titlekek[i].data(), 16) + "\n";
    }
    return s;
}

static Bytes partition(const char *magic, size_t entry_size, const Files &files) {
    std::string strings;
    std::vector<uint32_t> name_off;
    for (auto &f : files) name_off.push_back(uint32_t(strings.size())), strings += f.first + '\0';
    strings.resize(align(strings.size(), 0x20), '\0');
    size_t header = 16 + entry_size * files.size() + strings.size();
    Bytes b(header);
    memcpy(&b[0], magic, 4);
    put32(b, 4, uint32_t(files.size()));
    put32(b, 8, uint32_t(strings.size()));
    uint64_t off = 0;
    for (size_t i = 0; i < files.size(); i++) {
        size_t e = 16 + entry_size * i;
        put64(b, e, off);
        put64(b, e + 8, files[i].second.size());
        put32(b, e + 16, name_off[i]);
        if (entry_size == 0x40) {
            size_t hashed = files[i].second.size() < 0x200 ? files[i].second.size() : 0x200;
            put32(b, e + 20, uint32_t(hashed));
            nx::sha256(files[i].second.data(), hashed, &b[e + 0x20]);
        }
        off += files[i].second.size();
    }
    if (!strings.empty()) memcpy(&b[16 + entry_size * files.size()], strings.data(), strings.size());
    for (auto &f : files) b.insert(b.end(), f.second.begin(), f.second.end());
    return b;
}

Bytes pfs0(const Files &files) { return partition("PFS0", 0x18, files); }
Bytes hfs0(const Files &files) { return partition("HFS0", 0x40, files); }

Bytes xci(const Files &secure, bool key_area_prefix) {
    Bytes root = hfs0({{"update", hfs0({})}, {"normal", hfs0({})}, {"secure", hfs0(secure)}});
    size_t base = key_area_prefix ? 0x1000 : 0, root_off = 0xF000;
    Bytes b(base + root_off, 0);
    memcpy(&b[base + 0x100], "HEAD", 4);
    // (the root header's size: its own header part, as a card has it)
    uint32_t count = 3, strsize = 0;
    memcpy(&strsize, &root[8], 4);
    put64(b, base + 0x130, root_off);
    put64(b, base + 0x138, 16 + 0x40 * count + strsize);
    b.insert(b.end(), root.begin(), root.end());
    return b;
}

Bytes romfs(const Files &files) {
    // directories, parents before children; each directory's children sorted
    std::map<std::string, std::vector<std::string>> subdirs, dirfiles;
    std::map<std::string, const Bytes *> data;
    std::set<std::string> all{""};
    for (auto &f : files) {
        std::string p = f.first;
        data[p] = &f.second;
        size_t slash = p.find_last_of('/');
        std::string parent = slash == std::string::npos ? "" : p.substr(0, slash);
        dirfiles[parent].push_back(p);
        while (!parent.empty() && all.insert(parent).second) {
            size_t s = parent.find_last_of('/');
            std::string up = s == std::string::npos ? "" : parent.substr(0, s);
            subdirs[up].push_back(parent);
            parent = up;
        }
    }
    auto base = [](const std::string &p) { size_t s = p.find_last_of('/'); return s == std::string::npos ? p : p.substr(s + 1); };
    std::vector<std::string> dirs{""};
    for (size_t i = 0; i < dirs.size(); i++) {
        auto &k = subdirs[dirs[i]];
        std::sort(k.begin(), k.end());
        dirs.insert(dirs.end(), k.begin(), k.end());
    }
    std::map<std::string, uint32_t> doff, foff;
    uint32_t dsize = 0, fsize = 0;
    std::vector<std::string> flist;
    for (auto &d : dirs) {
        doff[d] = dsize, dsize += uint32_t(0x18 + align(base(d).size(), 4));
        auto &fs = dirfiles[d];
        std::sort(fs.begin(), fs.end());
        for (auto &f : fs) foff[f] = fsize, fsize += uint32_t(0x20 + align(base(f).size(), 4)), flist.push_back(f);
    }
    Bytes dtab(dsize, 0), ftab(fsize, 0);
    const uint32_t E = 0xFFFFFFFF;
    uint64_t data_size = 0;
    std::map<std::string, uint64_t> fdata;
    for (auto &f : flist) fdata[f] = data_size, data_size = align(data_size + data[f]->size(), 16);
    auto parent_of = [](const std::string &p) { size_t s = p.find_last_of('/'); return s == std::string::npos ? std::string() : p.substr(0, s); };
    for (auto &d : dirs) {
        uint32_t o = doff[d];
        auto &sd = subdirs[d];
        auto &sf = dirfiles[d];
        put32(dtab, o, d.empty() ? 0 : doff[parent_of(d)]);
        uint32_t sib = E;
        if (!d.empty()) {
            auto &peers = subdirs[parent_of(d)];
            auto it = std::find(peers.begin(), peers.end(), d);
            if (it + 1 != peers.end()) sib = doff[*(it + 1)];
        }
        put32(dtab, o + 4, sib);
        put32(dtab, o + 8, sd.empty() ? E : doff[sd[0]]);
        put32(dtab, o + 12, sf.empty() ? E : foff[sf[0]]);
        put32(dtab, o + 16, E);
        std::string n = d.empty() ? "" : base(d);
        put32(dtab, o + 20, uint32_t(n.size()));
        memcpy(&dtab[o + 0x18], n.data(), n.size());
        for (size_t i = 0; i < sf.size(); i++) {
            uint32_t fo = foff[sf[i]];
            put32(ftab, fo, o);
            put32(ftab, fo + 4, i + 1 < sf.size() ? foff[sf[i + 1]] : E);
            put64(ftab, fo + 8, fdata[sf[i]]);
            put64(ftab, fo + 16, data[sf[i]]->size());
            put32(ftab, fo + 24, E);
            std::string fn = base(sf[i]);
            put32(ftab, fo + 28, uint32_t(fn.size()));
            memcpy(&ftab[fo + 0x20], fn.data(), fn.size());
        }
    }
    uint64_t data_off = 0x200, dhash = align(data_off + data_size, 4);
    Bytes b(size_t(dhash), 0);
    for (auto &f : flist)
        if (!data[f]->empty()) memcpy(b.data() + data_off + fdata[f], data[f]->data(), data[f]->size());
    Bytes dh(4, 0xFF), fh(4, 0xFF);  // (one hash bucket each, empty: the importer walks the entries, not the hashes)
    uint64_t dmeta = dhash + 4, fhash = dmeta + dsize, fmeta = fhash + 4;
    b.insert(b.end(), dh.begin(), dh.end());
    b.insert(b.end(), dtab.begin(), dtab.end());
    b.insert(b.end(), fh.begin(), fh.end());
    b.insert(b.end(), ftab.begin(), ftab.end());
    put64(b, 0, 0x50);
    put64(b, 0x08, dhash), put64(b, 0x10, 4);
    put64(b, 0x18, dmeta), put64(b, 0x20, dsize);
    put64(b, 0x28, fhash), put64(b, 0x30, 4);
    put64(b, 0x38, fmeta), put64(b, 0x40, fsize);
    put64(b, 0x48, data_off);
    return b;
}

// The hashes of data's blocks; padded: partial last block hashed zero-padded (IVFC)
static Bytes block_hashes(const Bytes &data, size_t block, bool padded) {
    Bytes out;
    for (size_t off = 0; off < data.size() || (off == 0 && data.empty()); off += block) {
        size_t n = data.size() - off < block ? data.size() - off : block;
        Bytes blk(data.begin() + off, data.begin() + off + n);
        if (padded) blk.resize(block, 0);
        uint8_t h[32];
        nx::sha256(blk.data(), blk.size(), h);
        out.insert(out.end(), h, h + 32);
        if (data.empty()) break;
    }
    return out;
}

// A section's plain bytes and its FS header hash data
Bytes section_plain(const SectionSpec &s, uint32_t log2, uint8_t hash_data[0xF8]) {
    memset(hash_data, 0, 0xF8);
    if (!s.is_romfs) {
        const size_t block = 0x200;
        Bytes table = block_hashes(s.data, block, false);
        size_t data_off = align(table.size(), 0x200);
        Bytes b(data_off, 0);
        memcpy(b.data(), table.data(), table.size());
        b.insert(b.end(), s.data.begin(), s.data.end());
        b.resize(align(b.size(), 0x200), 0);
        Bytes hd(0xF8, 0);
        nx::sha256(table.data(), table.size(), hd.data());
        put32(hd, 0x20, block);
        put32(hd, 0x24, 2);
        put64(hd, 0x28, 0), put64(hd, 0x30, table.size());
        put64(hd, 0x38, data_off), put64(hd, 0x40, s.data.size());
        memcpy(hash_data, hd.data(), 0xF8);
        return b;
    }
    const size_t block = size_t(1) << log2;
    Bytes levels[6];
    levels[5] = s.data;
    for (int i = 4; i >= 0; i--) levels[i] = block_hashes(levels[i + 1], block, true);
    Bytes master = block_hashes(levels[0], block, true);
    Bytes b, hd(0xF8, 0);
    memcpy(&hd[0], "IVFC", 4);
    put32(hd, 4, 0x20000);
    put32(hd, 8, 0x20);
    put32(hd, 12, 7);
    for (int i = 0; i < 6; i++) {
        size_t off = align(b.size(), block);
        b.resize(off, 0);
        b.insert(b.end(), levels[i].begin(), levels[i].end());
        put64(hd, 0x10 + 0x18 * i, off);
        put64(hd, 0x18 + 0x18 * i, levels[i].size());
        put32(hd, 0x20 + 0x18 * i, log2);
    }
    memcpy(&hd[0xC0], master.data(), 32);
    b.resize(align(b.size(), 0x200), 0);
    memcpy(hash_data, hd.data(), 0xF8);
    return b;
}

Bytes bucket_tree(const std::vector<Bytes> &entries, const std::vector<uint64_t> &offsets, uint64_t end, size_t entry_size) {
    const size_t node = 0x4000, per_set = (node - 0x10) / entry_size, sets = (entries.size() + per_set - 1) / per_set;
    Bytes b((sets + 1) * node, 0);
    put32(b, 4, uint32_t(sets));
    put64(b, 8, end);
    for (size_t i = 0; i < sets; i++) {
        size_t first = i * per_set, count = std::min(per_set, entries.size() - first);
        put64(b, 0x10 + 8 * i, offsets[first]);
        size_t at = (i + 1) * node;
        put32(b, at, uint32_t(i));
        put32(b, at + 4, uint32_t(count));
        put64(b, at + 8, first + count < entries.size() ? offsets[first + count] : end);
        for (size_t k = 0; k < count; k++) memcpy(&b[at + 0x10 + k * entry_size], entries[first + k].data(), entry_size);
    }
    return b;
}

static void bktr_header(Bytes &fs, size_t at, uint64_t off, uint64_t size, size_t count) {
    put64(fs, at, off);
    put64(fs, at + 8, size);
    memcpy(&fs[at + 0x10], "BKTR", 4);
    put32(fs, at + 0x14, 1);
    put32(fs, at + 0x18, uint32_t(count));
}

// The update's RomFS section, encrypted; fills its FS header
static Bytes patch_section(const PatchSpec &p, uint32_t log2, const nx::Key128 &key, uint64_t at, Bytes &fs, std::mt19937_64 &rng) {
    uint8_t hd[0xF8];
    Bytes virt = section_plain({true, p.new_romfs}, log2, hd);
    // the base's chunks by content (first offset wins)
    std::map<Bytes, uint64_t> base_chunks;
    for (size_t o = 0; o + p.chunk <= p.base_plain.size(); o += p.chunk)
        base_chunks.emplace(Bytes(p.base_plain.begin() + o, p.base_plain.begin() + o + p.chunk), o);
    struct Ind { uint64_t virt, phys; uint32_t storage; };
    std::vector<Ind> ind;
    Bytes data;
    for (size_t o = 0; o < virt.size(); o += p.chunk) {
        size_t n = std::min(p.chunk, virt.size() - o);
        Bytes c(virt.begin() + o, virt.begin() + o + n);
        Ind e{o, 0, 1};
        auto it = n == p.chunk ? base_chunks.find(c) : base_chunks.end();
        if (it != base_chunks.end()) e.storage = 0, e.phys = it->second;
        else e.phys = data.size(), data.insert(data.end(), c.begin(), c.end());
        if (!p.split && !ind.empty() && ind.back().storage == e.storage && ind.back().phys + (o - ind.back().virt) == e.phys) continue;
        ind.push_back(e);
    }
    data.resize(align(data.size(), 16), 0);
    // AesCtrEx ranges over the data
    struct Ex { uint64_t off; bool enc; uint32_t gen; };
    std::vector<Ex> ex;
    size_t piece = align(p.piece, 16);
    for (size_t o = 0, i = 0; o < data.size() || ex.empty(); o += piece, i++) ex.push_back({o, i % 3 != 2, uint32_t(0x10000 + 7 * i)});
    // layout: data, the indirect table, the AesCtrEx table
    std::vector<Bytes> ie, ee;
    std::vector<uint64_t> io, eo;
    for (auto &e : ind) {
        Bytes b(0x14, 0);
        put64(b, 0, e.virt), put64(b, 8, e.phys), put32(b, 16, e.storage);
        ie.push_back(b), io.push_back(e.virt);
    }
    for (auto &e : ex) {
        Bytes b(0x10, 0);
        put64(b, 0, e.off), b[8] = e.enc ? 0 : 1, put32(b, 12, e.gen);
        ee.push_back(b), eo.push_back(e.off);
    }
    Bytes itab = bucket_tree(ie, io, virt.size(), 0x14), etab = bucket_tree(ee, eo, data.size(), 0x10);
    uint64_t ind_off = align(data.size(), 0x200), ex_off = ind_off + itab.size();
    Bytes sec = data;
    sec.resize(ind_off, 0);
    sec.insert(sec.end(), itab.begin(), itab.end());
    sec.insert(sec.end(), etab.begin(), etab.end());
    sec.resize(align(sec.size(), 0x200), 0);

    put16(fs, 0, 2);
    fs[2] = nx::FS_ROMFS, fs[3] = nx::HASH_IVFC, fs[4] = nx::ENC_CTR_EX;
    memcpy(&fs[8], hd, 0xF8);
    bktr_header(fs, 0x100, ind_off, itab.size(), ind.size());
    bktr_header(fs, 0x120, ex_off, etab.size(), ex.size());
    put32(fs, 0x140, 0x0F);  // (the tables' generation)
    put32(fs, 0x144, uint32_t(rng()));
    uint8_t upper[8], ctr[16];
    for (int k = 0; k < 8; k++) upper[k] = fs[0x147 - k];
    nx::AesCtr aes(key);
    // tables and padding: the header's counter; each data range: its generation's (plain ranges left as they are)
    nx::make_counter(ctr, upper, at + data.size());
    aes.crypt(sec.data() + data.size(), sec.size() - data.size(), ctr);
    for (size_t i = 0; i < ex.size(); i++) {
        uint64_t end = i + 1 < ex.size() ? ex[i + 1].off : data.size();
        if (!ex[i].enc || end == ex[i].off) continue;
        uint8_t u[8];
        memcpy(u, upper, 4);
        for (int k = 0; k < 4; k++) u[4 + k] = uint8_t(ex[i].gen >> (24 - 8 * k));
        nx::make_counter(ctr, u, at + ex[i].off);
        aes.crypt(sec.data() + ex[i].off, size_t(end - ex[i].off), ctr);
    }
    return sec;
}

Nca nca(const Keys &keys, const NcaSpec &spec, std::mt19937_64 &rng) {
    Nca out;
    Bytes h(0xC00, 0);
    memcpy(&h[0x200], spec.nca2 ? "NCA2" : "NCA3", 4);
    h[0x204] = 0;
    h[0x205] = uint8_t(spec.type);
    h[0x207] = 0;  // application key area
    put64(h, 0x210, spec.title_id);
    h[0x220] = spec.keygen;
    int rev = nx::key_revision(0, spec.keygen);
    nx::Key128 section_key;
    random_key(section_key, rng);
    memset(out.rights_id, 0, 16);
    if (spec.rights) {
        for (int i = 0; i < 8; i++) out.rights_id[i] = uint8_t(spec.title_id >> (56 - 8 * i));
        out.rights_id[15] = spec.keygen;
        memcpy(&h[0x230], out.rights_id, 16);
        out.title_key = section_key;
    } else {
        Bytes area = random_bytes(rng, 0x40);
        memcpy(&area[0x20], section_key.data(), 16);
        nx::Key128 unknown;  // (a revision the test keys don't have: the import must say which key is missing)
        random_key(unknown, rng);
        nx::aes_ecb_encrypt(rev < 4 ? keys.kak[rev] : unknown, area.data(), &h[0x300], 0x40);
    }
    Bytes body;
    uint64_t at = 0xC00;
    for (size_t i = 0; i < spec.sections.size(); i++) {
        Bytes fs(0x200, 0);
        if (spec.sections[i].patch) {
            Bytes enc = patch_section(*spec.sections[i].patch, spec.ivfc_block_log2, section_key, at, fs, rng);
            put32(h, 0x240 + 0x10 * i, uint32_t(at / 0x200));
            put32(h, 0x244 + 0x10 * i, uint32_t((at + enc.size()) / 0x200));
            nx::sha256(fs.data(), 0x200, &h[0x280 + 0x20 * i]);
            memcpy(&h[0x400 + 0x200 * i], fs.data(), 0x200);
            body.insert(body.end(), enc.begin(), enc.end());
            at += enc.size();
            continue;
        }
        uint8_t hd[0xF8];
        Bytes plain = section_plain(spec.sections[i], spec.ivfc_block_log2, hd);
        put16(fs, 0, 2);
        fs[2] = spec.sections[i].is_romfs ? nx::FS_ROMFS : nx::FS_PFS0;
        fs[3] = spec.sections[i].is_romfs ? nx::HASH_IVFC : nx::HASH_SHA256;
        fs[4] = nx::ENC_CTR;
        memcpy(&fs[8], hd, 0xF8);
        put32(fs, 0x140, 0);                   // generation
        put32(fs, 0x144, uint32_t(rng()));     // secure value
        uint8_t upper[8], ctr[16];
        for (int k = 0; k < 8; k++) upper[k] = fs[0x147 - k];
        nx::make_counter(ctr, upper, at);
        nx::AesCtr(section_key).crypt(plain.data(), plain.size(), ctr);
        put32(h, 0x240 + 0x10 * i, uint32_t(at / 0x200));
        put32(h, 0x244 + 0x10 * i, uint32_t((at + plain.size()) / 0x200));
        nx::sha256(fs.data(), 0x200, &h[0x280 + 0x20 * i]);
        memcpy(&h[0x400 + 0x200 * i], fs.data(), 0x200);
        body.insert(body.end(), plain.begin(), plain.end());
        at += plain.size();
    }
    put64(h, 0x208, at);
    if (spec.nca2) {
        nx::aes_xts_nintendo(keys.header_key, false, h.data(), 0x400, 0, 0x200);
        for (int i = 0; i < 4; i++) nx::aes_xts_nintendo(keys.header_key, false, &h[0x400 + 0x200 * i], 0x200, 0, 0x200);
    } else {
        nx::aes_xts_nintendo(keys.header_key, false, h.data(), 0xC00, 0, 0x200);
    }
    out.bytes = std::move(h);
    out.bytes.insert(out.bytes.end(), body.begin(), body.end());
    return out;
}

std::string nca_name(const Bytes &b, bool meta) {
    uint8_t h[32];
    nx::sha256(b.data(), b.size(), h);
    return nx::hex(h, 16) + (meta ? ".cnmt.nca" : ".nca");
}

Bytes cnmt(uint64_t title_id, uint32_t version, uint8_t meta_type, uint64_t application_id, const std::vector<CnmtRecord> &records) {
    const size_t ext = meta_type == nx::META_PATCH ? 0x18 : 0x10;
    Bytes b(0x20 + ext + 0x38 * records.size(), 0);
    put64(b, 0, title_id);
    put32(b, 8, version);
    b[0xC] = meta_type;
    put16(b, 0xE, uint16_t(ext));
    put16(b, 0x10, uint16_t(records.size()));
    put64(b, 0x20, meta_type == nx::META_PATCH ? application_id : title_id | 0x800);
    for (size_t i = 0; i < records.size(); i++) {
        size_t at = 0x20 + ext + 0x38 * i;
        nx::sha256(records[i].nca.data(), records[i].nca.size(), &b[at]);
        memcpy(&b[at + 0x20], &b[at], 16);
        uint64_t size = records[i].nca.size();
        for (int k = 0; k < 6; k++) b[at + 0x30 + k] = uint8_t(size >> (8 * k));
        b[at + 0x36] = records[i].type;
    }
    return b;
}

Bytes ticket(const uint8_t rights_id[16], const uint8_t enc_title_key[16], bool common) {
    Bytes b(4 + 0x100 + 0x3C + 0x180, 0);
    put32(b, 0, 0x010004);  // RSA-2048 SHA-256
    size_t body = 4 + 0x100 + 0x3C;
    memcpy(&b[body], "Root-CA00000003-XS00000020", 26);
    memcpy(&b[body + 0x40], enc_title_key, 16);
    b[body + 0x140] = 2;
    b[body + 0x141] = common ? 0 : 1;
    b[body + 0x145] = rights_id[15];
    memcpy(&b[body + 0x160], rights_id, 16);
    return b;
}

Game game(const Keys &keys, const GameSpec &spec, std::mt19937_64 &rng) {
    Game g;
    NcaSpec p;
    p.title_id = spec.meta_type == nx::META_PATCH ? (spec.title_id & ~0xFFFull) : spec.title_id;
    p.keygen = spec.keygen;
    p.rights = spec.rights;
    p.nca2 = spec.nca2;
    PatchSpec patch;
    if (spec.base_romfs) {  // an update: its RomFS as a patch over the base's section
        uint8_t hd[0xF8];
        patch.base_plain = section_plain({true, romfs(*spec.base_romfs)}, p.ivfc_block_log2, hd);
        patch.new_romfs = romfs(spec.romfs);
        patch.chunk = spec.patch_chunk;
        patch.split = spec.patch_split;
        p.sections = {{false, pfs0(spec.exefs)}, {true, {}, &patch}};
    } else {
        p.sections = {{false, pfs0(spec.exefs)}, {true, romfs(spec.romfs)}};
    }
    Nca program = nca(keys, p, rng);
    g.program_nca = program.bytes;
    g.title_key = program.title_key;
    memcpy(g.rights_id, program.rights_id, 16);
    char name[64];
    snprintf(name, sizeof name, "%s_%016llx.cnmt", spec.meta_type == nx::META_PATCH ? "Patch" : "Application",
             (unsigned long long)spec.title_id);
    Bytes meta_cnmt = cnmt(spec.title_id, spec.version, spec.meta_type, spec.title_id & ~0xFFFull, {{program.bytes, nx::CNT_PROGRAM}});
    NcaSpec m;
    m.type = nx::ContentType::Meta;
    m.title_id = spec.title_id;
    m.keygen = spec.keygen;
    m.sections = {{false, pfs0({{name, meta_cnmt}})}};
    Nca meta = nca(keys, m, rng);
    g.package.push_back({nca_name(program.bytes, false), program.bytes});
    g.package.push_back({nca_name(meta.bytes, true), meta.bytes});
    if (spec.rights && spec.with_ticket) {
        int rev = nx::key_revision(0, spec.keygen);
        uint8_t enc[16];
        nx::aes_ecb_encrypt(keys.titlekek[rev], program.title_key.data(), enc, 16);
        g.package.push_back({nx::hex(program.rights_id, 16) + ".tik", ticket(program.rights_id, enc, true)});
    }
    return g;
}

Files sample_exefs(std::mt19937_64 &rng) {
    Bytes main = random_bytes(rng, 5000);
    memcpy(main.data(), "NSO0", 4);
    return {{"main", main}, {"main.npdm", random_bytes(rng, 300)}, {"rtld", random_bytes(rng, 700)}, {"sdk", random_bytes(rng, 3000)},
            {"subsdk0", random_bytes(rng, 1000)}};
}

Files sample_romfs(std::mt19937_64 &rng) {
    return {{"System/Version.txt", Bytes{'1', '.', '0', '.', '0'}},
            {"Pack/Bootup.pack", random_bytes(rng, 20000)},
            {"Pack/Dungeon000.pack", random_bytes(rng, 7000)},
            {"Actor/Pack/Enemy_Bokoblin.sbactorpack", random_bytes(rng, 3333)},
            {"Actor/Pack/Empty.sbactorpack", Bytes{}},
            {"Model/Terrain/A-1.sbfres", random_bytes(rng, 9001)},
            {"Movie/Demo101.mp4", random_bytes(rng, 15)}};
}

}  // namespace synth
