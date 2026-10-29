#include "keys.h"

#include <cstdio>
#include <cstring>

namespace nx {

std::string key_name(const char *base, int rev) {
    char b[80];
    snprintf(b, sizeof b, "%s_%02x", base, rev);
    return b;
}

std::vector<uint8_t> read_small_file(const Reader &r, size_t max_size) {
    if (r.size() > max_size)
        fail(r.what() + " is too large to be a keys file", r.what() + " é grande demais para ser um arquivo de chaves");
    return r.read_vec(r.size(), 0);
}

static int hexval(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// The hex value [p, e) into out (exactly n bytes); false when it isn't that
static bool parse_hex(const uint8_t *p, const uint8_t *e, uint8_t *out, size_t n) {
    if (size_t(e - p) != n * 2) return false;
    for (size_t i = 0; i < n; i++) {
        int h = hexval(p[2 * i]), l = hexval(p[2 * i + 1]);
        if (h < 0 || l < 0) return false;
        out[i] = uint8_t(h << 4 | l);
    }
    return true;
}

static bool is_space(uint8_t c) { return c == ' ' || c == '\t' || c == '\r'; }

// Each "name = value" line of text: f(line number, name, value begin, value end). Blank lines and lines starting
// with ';' or '#' are skipped; a line without '=' (or ',', hactool's other separator) is malformed.
template <class F> static void for_each_line(const uint8_t *text, size_t n, const std::string &file, F f) {
    const uint8_t *p = text, *end = text + n;
    int line = 0;
    while (p < end) {
        const uint8_t *eol = static_cast<const uint8_t *>(memchr(p, '\n', size_t(end - p)));
        if (!eol) eol = end;
        line++;
        const uint8_t *a = p, *b = eol;
        p = eol < end ? eol + 1 : end;
        while (a < b && is_space(*a)) a++;
        while (b > a && is_space(b[-1])) b--;
        if (a == b || *a == ';' || *a == '#') continue;
        const uint8_t *sep = a;
        while (sep < b && *sep != '=' && *sep != ',') sep++;
        if (sep == b) {
            std::string l = std::to_string(line);
            fail(file + " line " + l + " is not \"name = value\"", file + ": a linha " + l + " não está no formato \"nome = valor\"");
        }
        const uint8_t *ne = sep, *vb = sep + 1;
        while (ne > a && is_space(ne[-1])) ne--;
        while (vb < b && is_space(*vb)) vb++;
        std::string name(reinterpret_cast<const char *>(a), size_t(ne - a));
        for (auto &c : name) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
        f(line, name, vb, b);
    }
}

[[noreturn]] static void bad_value(const std::string &file, int line, const std::string &name) {
    std::string l = std::to_string(line);
    fail(file + " line " + l + ": the value of " + name + " is not valid hex of the right length",
         file + ", linha " + l + ": o valor de " + name + " não é hexadecimal do tamanho certo");
}

template <size_t N> static void store(Secret<N> &k, const std::string &file, int line, const std::string &name,
                                      const uint8_t *vb, const uint8_t *ve) {
    if (!parse_hex(vb, ve, k.data(), N)) bad_value(file, line, name);
    k.set = true;
}

// "base_XX" with XX two hex digits below MAX_KEY_REV: the revision, else -1
static int indexed(const std::string &name, const char *base) {
    size_t n = strlen(base);
    if (name.size() != n + 3 || name.compare(0, n, base) != 0 || name[n] != '_') return -1;
    int h = hexval(uint8_t(name[n + 1])), l = hexval(uint8_t(name[n + 2]));
    if (h < 0 || l < 0 || (h << 4 | l) >= MAX_KEY_REV) return -1;
    return h << 4 | l;
}

void KeySet::parse_prod_keys(const uint8_t *text, size_t n) {
    static const char *const kak_names[3] = {"key_area_key_application", "key_area_key_ocean", "key_area_key_system"};
    const std::string file = "prod.keys";
    for_each_line(text, n, file, [&](int line, const std::string &name, const uint8_t *vb, const uint8_t *ve) {
        int rev;
        if (name == "header_key") return store(header_key_, file, line, name, vb, ve);
        if (name == "header_key_source") return store(header_key_source_, file, line, name, vb, ve);
        if (name == "header_kek_source") return store(header_kek_source_, file, line, name, vb, ve);
        if (name == "titlekek_source") return store(titlekek_source_, file, line, name, vb, ve);
        if (name == "aes_kek_generation_source") return store(aes_kek_generation_source_, file, line, name, vb, ve);
        if (name == "aes_key_generation_source") return store(aes_key_generation_source_, file, line, name, vb, ve);
        for (int k = 0; k < 3; k++) {
            if (name == std::string(kak_names[k]) + "_source") return store(kak_source_[k], file, line, name, vb, ve);
            if ((rev = indexed(name, kak_names[k])) >= 0) return store(kak_[k][rev], file, line, name, vb, ve);
        }
        if ((rev = indexed(name, "titlekek")) >= 0) return store(titlekek_[rev], file, line, name, vb, ve);
        if ((rev = indexed(name, "master_key")) >= 0) return store(master_key_[rev], file, line, name, vb, ve);
        // (any other key: not needed here, left alone)
    });
}

void KeySet::parse_title_keys(const uint8_t *text, size_t n) {
    const std::string file = "title.keys";
    for_each_line(text, n, file, [&](int line, const std::string &name, const uint8_t *vb, const uint8_t *ve) {
        uint8_t rid[16];
        if (!parse_hex(reinterpret_cast<const uint8_t *>(name.data()), reinterpret_cast<const uint8_t *>(name.data()) + name.size(), rid, 16)) {
            std::string l = std::to_string(line);
            fail(file + " line " + l + ": the rights id is not 32 hex digits", file + ", linha " + l + ": o rights id não tem 32 dígitos hexadecimais");
        }
        Key128 k;
        if (!parse_hex(vb, ve, k.data(), 16)) bad_value(file, line, "the title key");
        k.set = true;
        title_keys_[hex(rid, 16)] = k;
    });
}

// generate_kek: the key a source key becomes under a master key (kek seed, then the source, then the key seed)
static bool generate_kek(Key128 &out, const Key128 &src, const Key128 &master, const Key128 &kek_seed, const Key128 &key_seed) {
    if (!src.set || !master.set || !kek_seed.set) return false;
    Key128 kek, src_kek;
    aes_ecb_decrypt(master, kek_seed.data(), kek.data(), 16);
    aes_ecb_decrypt(kek, src.data(), src_kek.data(), 16);
    if (key_seed.set) aes_ecb_decrypt(src_kek, key_seed.data(), out.data(), 16);
    else out.b = src_kek.b;
    out.set = true;
    return true;
}

void KeySet::derive() {
    for (int rev = 0; rev < MAX_KEY_REV; rev++) {
        if (!master_key_[rev].set) continue;
        for (int k = 0; k < 3; k++)
            if (!kak_[k][rev].set)
                generate_kek(kak_[k][rev], kak_source_[k], master_key_[rev], aes_kek_generation_source_, aes_key_generation_source_);
        if (!titlekek_[rev].set && titlekek_source_.set) {
            aes_ecb_decrypt(master_key_[rev], titlekek_source_.data(), titlekek_[rev].data(), 16);
            titlekek_[rev].set = true;
        }
    }
    if (!header_key_.set && header_key_source_.set) {
        Key128 header_kek;
        if (generate_kek(header_kek, header_kek_source_, master_key_[0], aes_kek_generation_source_, aes_key_generation_source_)) {
            aes_ecb_decrypt(header_kek, header_key_source_.data(), header_key_.data(), 32);
            header_key_.set = true;
        }
    }
}

[[noreturn]] static void missing(const std::string &name, int rev) {
    if (rev > 0)
        fail("Missing " + name + ": your prod.keys are from an older firmware than this content. Dump the keys again from "
             "your console after updating it.",
             "Falta " + name + ": suas prod.keys são de um firmware mais antigo que este conteúdo. Extraia as chaves de "
             "novo do seu console depois de atualizá-lo.");
    fail("Missing " + name + " in prod.keys", "Falta " + name + " nas prod.keys");
}

const Key256 &KeySet::header_key() const {
    if (!header_key_.set) missing("header_key", 0);
    return header_key_;
}

const Key128 &KeySet::key_area_key(int index, int rev) const {
    static const char *const names[3] = {"key_area_key_application", "key_area_key_ocean", "key_area_key_system"};
    if (index < 0 || index > 2) corrupt("NCA key area index");
    if (rev < 0 || rev >= MAX_KEY_REV || !kak_[index][rev].set) missing(key_name(names[index], rev), rev);
    return kak_[index][rev];
}

const Key128 &KeySet::titlekek(int rev) const {
    if (rev < 0 || rev >= MAX_KEY_REV || !titlekek_[rev].set) missing(key_name("titlekek", rev), rev);
    return titlekek_[rev];
}

const Key128 *KeySet::title_key(const uint8_t rights_id[16]) const {
    auto it = title_keys_.find(hex(rights_id, 16));
    return it == title_keys_.end() ? nullptr : &it->second;
}

}  // namespace nx
