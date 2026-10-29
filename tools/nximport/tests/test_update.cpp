// Updates: base + update NSP through BKTR (IndirectStorage + AesCtrEx), against synthetic patches built from the base
// section and a changed RomFS. These prove the reader and the generator agree; the independent check on real files is
// the update's IVFC tree (every read) and verify.sh against hactool.
#include "bktr.h"
#include "nximport.h"
#include "synth.h"
#include "test.h"

#include <array>
#include <cstring>

using namespace nx;

std::string temp_dir();
void rm_tree(const std::string &p);
synth::Bytes read_file(const std::string &p);

namespace {

ReaderPtr mem(const synth::Bytes &b, const char *what) { return std::make_shared<MemReader>(b, what); }

struct UpdateFixture {
    std::mt19937_64 rng{1234};
    synth::Keys keys{rng};
    synth::Files exefs = synth::sample_exefs(rng), romfs = synth::sample_romfs(rng);
    synth::Files new_exefs, new_romfs;
    UpdateFixture() {
        new_exefs = synth::sample_exefs(rng);
        new_exefs.push_back({"subsdk1", synth::random_bytes(rng, 640)});
        new_romfs = romfs;
        new_romfs[0].second = {'1', '.', '6', '.', '0'};                         // System/Version.txt
        for (size_t i = 100; i < 300; i++) new_romfs[1].second[i] ^= 0x5A;     // Bootup.pack, same size
        new_romfs.erase(new_romfs.begin() + 2);                                 // Dungeon000.pack gone
        new_romfs.push_back({"Pack/AocMainField.pack", synth::random_bytes(rng, 4321)});
        new_romfs.push_back({"Model/Terrain/B-2.sbfres", romfs[5].second});      // (a copy: base bytes elsewhere)
    }
    synth::Game base(bool rights = false) {
        synth::GameSpec s;
        s.exefs = exefs, s.romfs = romfs, s.rights = rights;
        return synth::game(keys, s, rng);
    }
    synth::Game update(bool rights = false, size_t chunk = 0x200, bool split = false, const synth::Files *against = nullptr) {
        synth::GameSpec s;
        s.title_id = BOTW_PATCH_ID;
        s.meta_type = 0x81;
        s.version = 786432;
        s.exefs = new_exefs, s.romfs = new_romfs, s.rights = rights, s.keygen = rights ? 3 : 0;
        s.base_romfs = against ? against : &romfs;
        s.patch_chunk = chunk, s.patch_split = split;
        return synth::game(keys, s, rng);
    }
    ImportInput input(const synth::Files &base_pkg, const synth::Files &update_pkg, PackageKind kind = PackageKind::NSP) {
        ImportInput in;
        in.base = mem(kind == PackageKind::NSP ? synth::pfs0(base_pkg) : synth::xci(base_pkg, false), "base");
        in.base_kind = kind;
        in.update = mem(synth::pfs0(update_pkg), "update.nsp");
        std::string k = keys.prod_keys();
        in.prod_keys = mem(synth::Bytes(k.begin(), k.end()), "prod.keys");
        return in;
    }
    void check(const Importer &imp) {
        std::string dir = temp_dir(), out = dir + "/o";
        imp.extract_to(out, nullptr, nullptr);
        for (auto &f : new_exefs) CHECK(read_file(out + "/exefs/" + f.first) == f.second);
        for (auto &f : new_romfs) CHECK(read_file(out + "/romfs/" + f.first) == f.second);
        CHECK(imp.romfs().files.size() == new_romfs.size());
        CHECK(imp.exefs().size() == new_exefs.size());
        rm_tree(dir);
    }
};

}  // namespace

TEST(update_basic) {
    UpdateFixture fx;
    auto b = fx.base(), u = fx.update();
    Importer imp(fx.input(b.package, u.package));
    CHECK(imp.has_update());
    CHECK(imp.update_version() == 786432);
    CHECK(imp.version_txt() == "1.6.0");
    fx.check(imp);
    imp.verify_ncas(nullptr, nullptr);
}

TEST(update_over_xci_with_title_keys) {
    UpdateFixture fx;
    auto b = fx.base(), u = fx.update(true);
    Importer imp(fx.input(b.package, u.package, PackageKind::XCI));
    fx.check(imp);
}

TEST(update_many_entries_several_sets) {
    UpdateFixture fx;
    auto b = fx.base(), u = fx.update(false, 0x40, true);  // (one entry per 64 bytes: > 818 entries)
    Importer imp(fx.input(b.package, u.package));
    fx.check(imp);
}

TEST(update_against_another_base) {
    UpdateFixture fx;
    fx.new_romfs = fx.romfs;  // (the same layout: the update reads almost everything from the base)
    fx.new_romfs[0].second = {'1', '.', '6', '.', '0'};
    auto u = fx.update();
    {
        Importer imp(fx.input(fx.base().package, u.package));
        fx.check(imp);
    }
    // a base whose A-1.sbfres (kept as it is by the update: read from the base) differs
    synth::Files saved = fx.romfs;
    fx.romfs[5].second[100] ^= 1;
    auto other = fx.base();
    fx.romfs = saved;
    std::string dir = temp_dir();
    std::string e = ERROR_OF(Importer(fx.input(other.package, u.package)).extract_to(dir + "/o", nullptr, nullptr));
    rm_tree(dir);
    CHECK(contains(e, "integrity check failed"));
    CHECK(contains(e, "doesn't belong to this base game"));
}

TEST(update_package_checks) {
    UpdateFixture fx;
    auto b = fx.base(), u = fx.update();
    std::string e = ERROR_OF(Importer(fx.input(b.package, b.package)));  // (the base given as the update)
    CHECK(contains(e, "is not an update of Breath of the Wild"));
    CHECK(contains(e, "não é um update"));
    e = ERROR_OF(Importer(fx.input(u.package, u.package)));  // (the update given as the base)
    CHECK(contains(e, "is not Breath of the Wild"));
}

TEST(update_corruption) {
    UpdateFixture fx;
    auto b = fx.base(), u = fx.update();
    synth::Bytes base_nsp = synth::pfs0(b.package), upd = synth::pfs0(u.package);
    std::string k = fx.keys.prod_keys(), dir = temp_dir();
    std::mt19937_64 pick(5);
    int refused = 0;
    const int N = 1200;
    for (int i = 0; i < N; i++) {
        synth::Bytes x = upd;
        size_t at = i < 400 ? size_t(i) * 13 % 0x2000 : size_t(pick() % x.size());
        x[at] ^= uint8_t(1u << (pick() % 8));
        ImportInput in;
        in.base = mem(base_nsp, "base.nsp");
        in.update = mem(x, "update.nsp");
        in.prod_keys = mem(synth::Bytes(k.begin(), k.end()), "prod.keys");
        std::string out = dir + "/o";
        rm_tree(out);
        try {
            Importer imp(in);
            imp.extract_to(out, nullptr, nullptr);
        } catch (const Error &) {
            refused++;
            continue;
        }
        bool same = true;
        for (auto &f : fx.new_exefs) same &= read_file(out + "/exefs/" + f.first) == f.second;
        for (auto &f : fx.new_romfs) same &= read_file(out + "/romfs/" + f.first) == f.second;
        if (!same) fprintf(stderr, "  flip at 0x%zx extracted different files\n", at);
        CHECK(same);
    }
    fprintf(stderr, "  %d flips: %d refused\n", N, refused);
    rm_tree(dir);
}

// The tree checks, on hand-made tables
static synth::Bytes indirect_tree(const std::vector<std::array<uint64_t, 3>> &e, uint64_t end, uint8_t bh[0x10]) {
    std::vector<synth::Bytes> ent;
    std::vector<uint64_t> off;
    for (auto &x : e) {
        synth::Bytes b(0x14, 0);
        for (int i = 0; i < 8; i++) b[i] = uint8_t(x[0] >> (8 * i)), b[8 + i] = uint8_t(x[1] >> (8 * i));
        b[16] = uint8_t(x[2]);
        ent.push_back(b), off.push_back(x[0]);
    }
    memset(bh, 0, 0x10);
    memcpy(bh, "BKTR", 4);
    bh[4] = 1;
    bh[8] = uint8_t(e.size()), bh[9] = uint8_t(e.size() >> 8);
    return synth::bucket_tree(ent, off, end, 0x14);
}

TEST(bktr_tree_checks) {
    uint8_t bh[0x10];
    uint64_t end;
    auto ok = indirect_tree({{0, 0, 0}, {0x100, 0, 1}, {0x300, 0x500, 0}}, 0x400, bh);
    auto v = read_indirect_tree(MemReader(ok, "t"), bh, &end);
    CHECK(v.size() == 3 && end == 0x400 && v[1].storage == 1 && v[2].phys == 0x500);

    auto bad_order = indirect_tree({{0, 0, 0}, {0x300, 0, 1}, {0x100, 0, 0}}, 0x400, bh);
    CHECK(contains(ERROR_OF(read_indirect_tree(MemReader(bad_order, "t"), bh, &end)), "order"));
    auto not_zero = indirect_tree({{0x10, 0, 0}}, 0x400, bh);
    CHECK(contains(ERROR_OF(read_indirect_tree(MemReader(not_zero, "t"), bh, &end)), "first entry"));
    auto storage = indirect_tree({{0, 0, 2}}, 0x400, bh);
    CHECK(contains(ERROR_OF(read_indirect_tree(MemReader(storage, "t"), bh, &end)), "storage index"));
    auto past_end = indirect_tree({{0, 0, 0}, {0x500, 0, 0}}, 0x400, bh);
    CHECK(!ERROR_OF(read_indirect_tree(MemReader(past_end, "t"), bh, &end)).empty());
    {  // the FS header's count differs from the table's
        auto t = indirect_tree({{0, 0, 0}, {0x100, 0, 1}}, 0x400, bh);
        bh[8] = 3;
        CHECK(!ERROR_OF(read_indirect_tree(MemReader(t, "t"), bh, &end)).empty());
    }
    {  // a set's start differing from the node's offset for it
        auto t = indirect_tree({{0, 0, 0}, {0x100, 0, 1}}, 0x400, bh);
        t[0x10] = 0x20;
        CHECK(!ERROR_OF(read_indirect_tree(MemReader(t, "t"), bh, &end)).empty());
    }
    {  // truncated table
        auto t = indirect_tree({{0, 0, 0}}, 0x400, bh);
        t.resize(t.size() - 1);
        CHECK(!ERROR_OF(read_indirect_tree(MemReader(t, "t"), bh, &end)).empty());
    }
    {  // not a BKTR header
        auto t = indirect_tree({{0, 0, 0}}, 0x400, bh);
        bh[0] = 'X';
        CHECK(!ERROR_OF(read_indirect_tree(MemReader(t, "t"), bh, &end)).empty());
    }
}
