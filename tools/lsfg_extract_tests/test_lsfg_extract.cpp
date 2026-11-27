// Host tests of the frame generation's shader extraction (android/.../cpp/lsfg_extract.cpp, lsfg_translate.cpp).
// No Lossless Scaling file is used: a synthetic PE32+ with RT_RCDATA resources holds fake shaders - 4 bytes "FAKE" and
// the bindings it should have - which a fake translator turns into a small compute SPIR-V module with those bindings.
// The real translator (DXVK's dxbc) is only fed garbage here: it must fail cleanly.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "lsfg_extract.h"

namespace lsfg {
bool dxbc_translate(const std::vector<uint8_t> &dxbc, std::vector<uint32_t> &out, std::string &err);
}

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

// ---- a compute SPIR-V module with the given bindings: u uniform buffers, s samplers, t sampled images, i storage images
static std::vector<uint32_t> spirv_with(int u, int s, int t, int im, bool compute = true) {
    std::vector<uint32_t> w = {0x07230203u, 0x00010500u, 0, 0, 0};
    uint32_t next = 1;
    auto op = [&](uint32_t code, std::vector<uint32_t> args) {
        w.push_back(uint32_t((args.size() + 1) << 16) | code);
        w.insert(w.end(), args.begin(), args.end());
    };
    op(17, {1});                       // OpCapability Shader
    op(17, {56});                      // OpCapability StorageImageWriteWithoutFormat
    op(14, {0, 1});                    // OpMemoryModel Logical GLSL450
    uint32_t fn = next++;
    if (compute) op(15, {5, fn, 0x6e69616d, 0});  // OpEntryPoint GLCompute %fn "main"
    else op(15, {4, fn, 0x6e69616d, 0});          // (Fragment)
    uint32_t f32 = next++, smp = next++, img = next++, simg = next++, st = next++;
    op(22, {f32, 32});                             // OpTypeFloat 32
    op(26, {smp});                                 // OpTypeSampler
    op(25, {img, f32, 1, 0, 0, 0, 1, 0});          // OpTypeImage 2D sampled
    op(25, {simg, f32, 1, 0, 0, 0, 2, 0});         // OpTypeImage 2D storage
    op(30, {st, f32});                             // OpTypeStruct { float }
    uint32_t p_smp = next++, p_img = next++, p_simg = next++, p_st = next++;
    op(32, {p_smp, 0, smp});                       // OpTypePointer UniformConstant
    op(32, {p_img, 0, img});
    op(32, {p_simg, 0, simg});
    op(32, {p_st, 2, st});                         // OpTypePointer Uniform
    uint32_t binding = 0;
    auto var = [&](uint32_t ptr, uint32_t sc) {
        uint32_t v = next++;
        op(71, {v, 33, binding++});                // OpDecorate Binding
        op(59, {ptr, v, sc});                      // OpVariable
    };
    for (int k = 0; k < u; k++) var(p_st, 2);
    for (int k = 0; k < s; k++) var(p_smp, 0);
    for (int k = 0; k < t; k++) var(p_img, 0);
    for (int k = 0; k < im; k++) var(p_simg, 0);
    w[3] = next;
    return w;
}

static bool fake_translate(const std::vector<uint8_t> &d, std::vector<uint32_t> &out, std::string &err) {
    if (d.size() < 8 || memcmp(d.data(), "FAKE", 4)) { err = "not a fake shader"; return false; }
    if (d[4] == 0xFF) { err = "refused"; return false; }
    out = spirv_with(d[4], d[5], d[6], d[7]);
    return true;
}

// ---- a PE32+ with a .rsrc section: RT_RCDATA (10) -> id -> language 0 -> data
static std::vector<uint8_t> make_pe(const std::map<uint32_t, std::vector<uint8_t>> &res) {
    const uint32_t file_align = 0x200, sect_rva = 0x1000, sect_off = 0x400;
    // the resource section, built at RVA sect_rva
    std::vector<uint8_t> r;
    auto u16 = [](std::vector<uint8_t> &v, size_t at, uint16_t x) { v[at] = uint8_t(x), v[at + 1] = uint8_t(x >> 8); };
    auto u32 = [](std::vector<uint8_t> &v, size_t at, uint32_t x) { for (int i = 0; i < 4; i++) v[at + i] = uint8_t(x >> (8 * i)); };
    size_t n = res.size();
    size_t root = 0, l2 = 16 + 8, l3 = l2 + 16 + 8 * n, data_entries = l3 + n * (16 + 8), blobs = data_entries + n * 16;
    r.resize(blobs);
    u16(r, root + 14, 1);                          // root: one id entry (type 10)
    u32(r, root + 16, 10);
    u32(r, root + 20, 0x80000000u | uint32_t(l2));
    u16(r, l2 + 14, uint16_t(n));                  // the names
    size_t k = 0;
    for (auto &e : res) {
        size_t l3k = l3 + k * (16 + 8), de = data_entries + k * 16;
        u32(r, l2 + 16 + 8 * k, e.first);
        u32(r, l2 + 16 + 8 * k + 4, 0x80000000u | uint32_t(l3k));
        u16(r, l3k + 14, 1);                       // one language
        u32(r, l3k + 16, 0);
        u32(r, l3k + 20, uint32_t(de));
        while (r.size() % 4) r.push_back(0);
        u32(r, de, sect_rva + uint32_t(r.size()));  // the data entry: RVA, size
        u32(r, de + 4, uint32_t(e.second.size()));
        r.insert(r.end(), e.second.begin(), e.second.end());
        k++;
    }
    while (r.size() % file_align) r.push_back(0);
    std::vector<uint8_t> f(sect_off, 0);
    f[0] = 'M', f[1] = 'Z';
    u32(f, 0x3C, 0x80);                            // e_lfanew
    size_t pe = 0x80;
    f[pe] = 'P', f[pe + 1] = 'E';
    size_t fh = pe + 4;
    u16(f, fh, 0x8664);                            // Machine
    u16(f, fh + 2, 1);                             // NumberOfSections
    u16(f, fh + 16, 240);                          // SizeOfOptionalHeader
    u16(f, fh + 18, 0x2022);                       // Characteristics: executable, large address aware, DLL
    size_t oh = fh + 20;
    u16(f, oh, 0x20B);                             // PE32+
    u32(f, oh + 16, 0);                            // AddressOfEntryPoint
    u32(f, oh + 32, 0x1000);                       // SectionAlignment
    u32(f, oh + 36, file_align);                   // FileAlignment
    u16(f, oh + 48, 6);                            // MajorSubsystemVersion
    u32(f, oh + 56, sect_rva + uint32_t(r.size()));  // SizeOfImage
    u32(f, oh + 60, sect_off);                     // SizeOfHeaders
    u16(f, oh + 68, 2);                            // Subsystem
    u32(f, oh + 108, 16);                          // NumberOfRvaAndSizes
    u32(f, oh + 112 + 2 * 8, sect_rva);            // DataDirectory[RESOURCE]
    u32(f, oh + 112 + 2 * 8 + 4, uint32_t(r.size()));
    size_t sh = oh + 240;
    memcpy(&f[sh], ".rsrc", 5);
    u32(f, sh + 8, uint32_t(r.size()));            // VirtualSize
    u32(f, sh + 12, sect_rva);
    u32(f, sh + 16, uint32_t(r.size()));           // SizeOfRawData
    u32(f, sh + 20, sect_off);
    u32(f, sh + 36, 0x40000040u);                  // initialized data, readable
    f.insert(f.end(), r.begin(), r.end());
    return f;
}

struct Expect { const char *name; int counts[4]; };
static const Expect kExpect[] = {
#include "lsfg_expect.inc"
};

static std::map<uint32_t, std::vector<uint8_t>> good_resources() {
    std::map<uint32_t, std::vector<uint8_t>> res;
    for (auto &s : lsfg::shader_table())
        for (auto &e : kExpect)
            if (!strcmp(e.name, s.name)) {
                std::vector<uint8_t> d = {'F', 'A', 'K', 'E', uint8_t(e.counts[0]), uint8_t(e.counts[1]), uint8_t(e.counts[2]), uint8_t(e.counts[3])};
                res[s.id] = d;
            }
    res[999] = {1, 2, 3};  // (an unrelated resource)
    return res;
}

static std::string tmpdir() {
    char t[] = "/tmp/lsfgtestXXXXXX";
    return mkdtemp(t);
}
static bool exists(const std::string &p) { struct stat st; return stat(p.c_str(), &st) == 0; }

static void test_bindings() {
    int c[4];
    std::vector<std::string> caps;
    std::string err;
    CHECK(lsfg::spirv_bindings(spirv_with(1, 2, 9, 3), c, caps, err));
    CHECK(c[0] == 1 && c[1] == 2 && c[2] == 9 && c[3] == 3);
    CHECK(caps.size() == 2);
    CHECK(!lsfg::spirv_bindings(spirv_with(0, 1, 1, 1, false), c, caps, err));  // (no compute entry point)
    auto w = spirv_with(1, 1, 1, 1);
    w.resize(w.size() - 2);  // (truncated)
    CHECK(!lsfg::spirv_bindings(w, c, caps, err));
    CHECK(!lsfg::spirv_bindings({1, 2, 3}, c, caps, err));
    // every prefix and a bit flip at every word: no crash, no out-of-bounds (ASan)
    auto full = spirv_with(1, 2, 3, 4);
    for (size_t i = 0; i <= full.size(); i++) {
        std::vector<uint32_t> p(full.begin(), full.begin() + long(i));
        lsfg::spirv_bindings(p, c, caps, err);
    }
    for (size_t i = 0; i < full.size(); i++)
        for (uint32_t bit : {1u, 0x10000u, 0x80000000u}) {
            auto q = full;
            q[i] ^= bit;
            lsfg::spirv_bindings(q, c, caps, err);
        }
}

static void test_extract() {
    std::string root = tmpdir();
    lsfg::Report rep;
    auto pe = make_pe(good_resources());
    CHECK(lsfg::extract(pe.data(), pe.size(), root, fake_translate, nullptr, rep) == lsfg::OK);
    fprintf(stderr, "extract: %s\n", rep.detail.c_str());
    CHECK(rep.shaders == int(lsfg::shader_table().size()));
    CHECK(rep.dll_sha256.size() == 64);
    std::string sum;
    std::vector<std::string> caps;
    CHECK(lsfg::installed(root, &sum, &caps));
    CHECK(caps.size() == 2);
    CHECK(exists(root + "/lsfg/spirv/alpha_0_.spv") && exists(root + "/lsfg/spirv/p_generate.spv"));
    CHECK(!exists(root + "/lsfg/spirv.importing") && !exists(root + "/lsfg/spirv.old"));

    // a shader missing: refused, the installed set untouched
    auto res = good_resources();
    res.erase(256);
    auto pe2 = make_pe(res);
    CHECK(lsfg::extract(pe2.data(), pe2.size(), root, fake_translate, nullptr, rep) == lsfg::MISSING);
    CHECK(rep.detail.find("generate") != std::string::npos);
    CHECK(lsfg::installed(root, nullptr));
    // a shader that doesn't translate
    res = good_resources();
    res[267][4] = 0xFF;
    auto pe3 = make_pe(res);
    CHECK(lsfg::extract(pe3.data(), pe3.size(), root, fake_translate, nullptr, rep) == lsfg::TRANSLATE);
    // the wrong bindings
    res = good_resources();
    res[267][7] = 9;
    auto pe4 = make_pe(res);
    CHECK(lsfg::extract(pe4.data(), pe4.size(), root, fake_translate, nullptr, rep) == lsfg::BINDINGS);
    // the validator saying no
    auto no = [](const std::vector<uint32_t> &, std::string &e) { e = "nope"; return false; };
    CHECK(lsfg::extract(pe.data(), pe.size(), root, fake_translate, no, rep) == lsfg::INVALID);
    // not a PE
    std::vector<uint8_t> junk(4096, 0x41);
    CHECK(lsfg::extract(junk.data(), junk.size(), root, fake_translate, nullptr, rep) == lsfg::NOT_PE);
    CHECK(lsfg::installed(root, nullptr));  // (still the first set)

    // a PE cut short and bytes flipped: no crash
    for (size_t cut = 64; cut < pe.size(); cut += 97) lsfg::extract(pe.data(), cut, root, fake_translate, nullptr, rep);
    for (size_t i = 0; i < 0x600 && i < pe.size(); i += 3) {
        auto q = pe;
        q[i] ^= 0x5A;
        lsfg::extract(q.data(), q.size(), root, fake_translate, nullptr, rep);
    }

    // recovery: an interrupted import's stage goes; a swap cut between its renames gets the old set back
    mkdir((root + "/lsfg/spirv.importing").c_str(), 0700);
    lsfg::recover(root);
    CHECK(!exists(root + "/lsfg/spirv.importing"));
    CHECK(rename((root + "/lsfg/spirv").c_str(), (root + "/lsfg/spirv.old").c_str()) == 0);
    CHECK(!lsfg::installed(root, nullptr));
    lsfg::recover(root);
    CHECK(lsfg::installed(root, nullptr));
    // a set from another extractor version isn't taken
    {
        std::string m = root + "/lsfg/spirv/MANIFEST";
        FILE *f = fopen(m.c_str(), "r+");
        CHECK(f);
        std::string s;
        char b[4096];
        size_t k;
        while ((k = fread(b, 1, sizeof b, f))) s.append(b, k);
        fclose(f);
        auto at = s.find("extractor ");
        s.replace(at, 11, "extractor 0");
        f = fopen(m.c_str(), "w");
        fwrite(s.data(), 1, s.size(), f);
        fclose(f);
        CHECK(!lsfg::installed(root, nullptr));
    }
    lsfg::remove_all(root);
    CHECK(!exists(root + "/lsfg"));
    rmdir(root.c_str());
}

static void test_dxbc_garbage() {
    std::vector<uint32_t> out;
    std::string err;
    std::vector<std::vector<uint8_t>> inputs = {
        {}, {'D', 'X', 'B', 'C'}, std::vector<uint8_t>(64, 0), std::vector<uint8_t>(4096, 0xFF),
    };
    std::vector<uint8_t> hdr(256, 0);  // ("DXBC", a checksum, version, a size and chunk count that lie)
    memcpy(hdr.data(), "DXBC", 4);
    hdr[20] = 1, hdr[24] = 0xFF, hdr[25] = 0xFF, hdr[28] = 200;
    inputs.push_back(hdr);
    for (auto &in : inputs) {
        bool ok = lsfg::dxbc_translate(in, out, err);
        CHECK(!ok);
    }
    fprintf(stderr, "dxbc on garbage: %s\n", err.c_str());
}

int main() {
    test_bindings();
    test_extract();
    test_dxbc_garbage();
    if (fails) { fprintf(stderr, "%d failed\n", fails); return 1; }
    fprintf(stderr, "all passed\n");
    return 0;
}
