#include "container.h"
#include "romfs.h"
#include "synth.h"
#include "test.h"

#include <cstring>

using namespace nx;

static ReaderPtr mem(const synth::Bytes &b, const char *what = "test") { return std::make_shared<MemReader>(b, what); }

TEST(pfs0_round_trip) {
    std::mt19937_64 rng(1);
    synth::Files f = {{"a", synth::random_bytes(rng, 100)}, {"main.npdm", {}}, {"c", synth::random_bytes(rng, 7)}};
    auto e = parse_pfs0(mem(synth::pfs0(f)));
    REQUIRE(e.size() == 3);
    for (size_t i = 0; i < 3; i++) {
        CHECK(e[i].name == f[i].first);
        CHECK(e[i].data->read_vec(e[i].data->size(), 0) == f[i].second);
    }
    CHECK(parse_pfs0(mem(synth::pfs0({}))).empty());
}

TEST(pfs0_rejects_bad_names_and_bounds) {
    std::mt19937_64 rng(2);
    CHECK(!ERROR_OF(parse_pfs0(mem(synth::pfs0({{"../x", {1}}})))).empty());
    CHECK(!ERROR_OF(parse_pfs0(mem(synth::pfs0({{"a/b", {1}}})))).empty());
    CHECK(!ERROR_OF(parse_pfs0(mem(synth::pfs0({{"..", {1}}})))).empty());
    CHECK(!ERROR_OF(parse_pfs0(mem(synth::pfs0({{"x", {1}}, {"x", {2}}})))).empty());
    auto b = synth::pfs0({{"x", synth::random_bytes(rng, 50)}});
    b.resize(b.size() - 1);  // (the entry runs past the end)
    CHECK(!ERROR_OF(parse_pfs0(mem(b))).empty());
    auto c = synth::pfs0({{"x", {1}}});
    c[4] = 0xFF, c[5] = 0xFF, c[6] = 0xFF, c[7] = 0x7F;  // absurd count
    CHECK(!ERROR_OF(parse_pfs0(mem(c))).empty());
    CHECK(contains(ERROR_OF(parse_pfs0(mem(synth::hfs0({})))), "not a PFS0"));
}

TEST(hfs0_and_xci) {
    std::mt19937_64 rng(3);
    synth::Files secure = {{"x.nca", synth::random_bytes(rng, 1000)}, {"y.tik", synth::random_bytes(rng, 64)}};
    for (bool prefix : {false, true}) {
        auto files = open_package(mem(synth::xci(secure, prefix), "game.xci"), PackageKind::XCI);
        REQUIRE(files.size() == 2);
        CHECK(files[0].name == "x.nca");
        CHECK(files[0].data->read_vec(1000, 0) == secure[0].second);
    }
    CHECK(contains(ERROR_OF(open_package(mem(synth::Bytes(0x4000, 0), "z.xci"), PackageKind::XCI)), "not an XCI"));
}

TEST(nsz_refused) {
    auto b = synth::pfs0({{"0123.ncz", {1, 2, 3}}});
    std::string e = ERROR_OF(open_package(mem(b, "game.nsz"), PackageKind::NSP));
    CHECK(contains(e, "NSZ/XCZ"));
    CHECK(contains(e, "ainda não suportado"));
}

TEST(romfs_listing) {
    std::mt19937_64 rng(4);
    auto files = synth::sample_romfs(rng);
    MemReader img(synth::romfs(files), "romfs");
    RomfsListing l = list_romfs(img);
    REQUIRE(l.files.size() == files.size());
    for (auto &f : files) {
        bool found = false;
        for (auto &g : l.files)
            if (g.path == f.first) {
                found = true;
                CHECK(img.read_vec(g.size, g.offset) == f.second);
            }
        CHECK(found);
    }
    std::vector<std::string> want = {"Actor", "Model", "Movie", "Pack", "System", "Actor/Pack", "Model/Terrain"};
    for (auto &d : want) CHECK(std::find(l.dirs.begin(), l.dirs.end(), d) != l.dirs.end());
}

TEST(romfs_rejects_cycles_and_bad_names) {
    std::mt19937_64 rng(5);
    synth::Bytes img = synth::romfs({{"a/b", {1}}, {"c", {2}}});
    uint64_t dmeta = 0, fmeta = 0;
    for (int i = 0; i < 8; i++) dmeta |= uint64_t(img[0x18 + i]) << (8 * i), fmeta |= uint64_t(img[0x38 + i]) << (8 * i);
    {  // root's first child dir pointing back at the root
        auto b = img;
        memset(&b[dmeta + 8], 0, 4);
        CHECK(contains(ERROR_OF(list_romfs(MemReader(b, "r"))), "cycle"));
    }
    {  // a file whose sibling is itself
        auto b = img;
        uint32_t first = 0;
        memcpy(&first, &b[dmeta + 12], 4);
        memcpy(&b[fmeta + first + 4], &first, 4);
        CHECK(contains(ERROR_OF(list_romfs(MemReader(b, "r"))), "cycle"));
    }
    {  // a name with ".."
        auto b = synth::romfs({{"..", {1}}});
        CHECK(!ERROR_OF(list_romfs(MemReader(b, "r"))).empty());
    }
    {  // a file's data past the image
        auto b = img;
        uint32_t first = 0;
        memcpy(&first, &b[dmeta + 12], 4);
        b[fmeta + first + 16 + 7] = 0x10;
        CHECK(!ERROR_OF(list_romfs(MemReader(b, "r"))).empty());
    }
}
