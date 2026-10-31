// The whole import over synthetic packages: NSP and XCI, key area and title key crypto (ticket, title.keys, the
// fallback between them), NCA2, the refusals, and the extracted files against what went in.
#include "nximport.h"
#include "synth.h"
#include "test.h"

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <ftw.h>
#include <iterator>
#include <unistd.h>

using namespace nx;

static ReaderPtr mem(const synth::Bytes &b, const char *what) { return std::make_shared<MemReader>(b, what); }
static ReaderPtr text(const std::string &s, const char *what) { return mem(synth::Bytes(s.begin(), s.end()), what); }

std::string temp_dir() {
    const char *t = getenv("TMPDIR");
    std::string tmpl = std::string(t && *t ? t : "/tmp") + "/nximport-test-XXXXXX";
    char *d = mkdtemp(&tmpl[0]);
    if (!d) throw std::runtime_error("mkdtemp");
    return d;
}
static int rm_entry(const char *p, const struct stat *, int, struct FTW *) { return remove(p); }
void rm_tree(const std::string &p) { nftw(p.c_str(), rm_entry, 16, FTW_DEPTH | FTW_PHYS); }

synth::Bytes read_file(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return synth::Bytes(std::istreambuf_iterator<char>(f), {});
}

struct Fixture {
    std::mt19937_64 rng{42};
    synth::Keys keys{rng};
    synth::Files exefs = synth::sample_exefs(rng), romfs = synth::sample_romfs(rng);
    synth::GameSpec spec() {
        synth::GameSpec s;
        s.exefs = exefs;
        s.romfs = romfs;
        return s;
    }
    ImportInput input(const synth::Files &package, PackageKind kind = PackageKind::NSP, const std::string &title_keys = "") {
        ImportInput in;
        in.base = mem(kind == PackageKind::NSP ? synth::pfs0(package) : synth::xci(package, false), kind == PackageKind::NSP ? "base.nsp" : "game.xci");
        in.base_kind = kind;
        in.prod_keys = text(keys.prod_keys(), "prod.keys");
        if (!title_keys.empty()) in.title_keys = text(title_keys, "title.keys");
        return in;
    }
    // extracts and checks every file against what went in
    void check_extract(const Importer &imp) {
        std::string dir = temp_dir();
        std::string out = dir + "/0100F2C0115B6000";
        uint64_t last = 0, total = 0;
        imp.extract_to(out, [&](uint64_t d, uint64_t t) { last = d, total = t; }, nullptr);
        CHECK(last == total);
        CHECK(total == imp.total_bytes());
        for (auto &f : exefs) CHECK(read_file(out + "/exefs/" + f.first) == f.second);
        for (auto &f : romfs) CHECK(read_file(out + "/romfs/" + f.first) == f.second);
        rm_tree(dir);
    }
};

TEST(import_nsp_key_area) {
    Fixture fx;
    auto g = synth::game(fx.keys, fx.spec(), fx.rng);
    Importer imp(fx.input(g.package));
    CHECK(imp.exefs().size() == fx.exefs.size());
    CHECK(imp.romfs().files.size() == fx.romfs.size());
    CHECK(imp.version_txt() == "1.4.3");
    CHECK(!imp.has_update());
    fx.check_extract(imp);
    imp.verify_ncas(nullptr, nullptr);
    for (auto &l : imp.describe()) CHECK(!contains(l, hex(fx.keys.header_key.data(), 32)));
}

TEST(import_xci) {
    Fixture fx;
    auto g = synth::game(fx.keys, fx.spec(), fx.rng);
    Importer imp(fx.input(g.package, PackageKind::XCI));
    fx.check_extract(imp);
}

TEST(import_nca2_and_key_generation) {
    Fixture fx;
    auto s = fx.spec();
    s.nca2 = true;
    s.keygen = 3;  // (revision 2)
    auto g = synth::game(fx.keys, s, fx.rng);
    Importer imp(fx.input(g.package));
    fx.check_extract(imp);
}

TEST(import_title_key_from_ticket) {
    Fixture fx;
    auto s = fx.spec();
    s.rights = true;
    s.keygen = 2;
    auto g = synth::game(fx.keys, s, fx.rng);
    Importer imp(fx.input(g.package));
    fx.check_extract(imp);
}

static std::string title_keys_line(const synth::Game &g, const synth::Keys &keys, int rev, bool wrong = false) {
    uint8_t enc[16];
    aes_ecb_encrypt(keys.titlekek[rev], g.title_key.data(), enc, 16);
    if (wrong) enc[0] ^= 1;
    return hex(g.rights_id, 16) + " = " + hex(enc, 16) + "\n";
}

TEST(import_title_key_from_title_keys) {
    Fixture fx;
    auto s = fx.spec();
    s.rights = true;
    s.with_ticket = false;
    auto g = synth::game(fx.keys, s, fx.rng);
    std::string e = ERROR_OF(Importer(fx.input(g.package)));
    CHECK(contains(e, "no title key"));
    CHECK(contains(e, "nenhuma nas title.keys"));
    Importer imp(fx.input(g.package, PackageKind::NSP, title_keys_line(g, fx.keys, 0)));
    fx.check_extract(imp);
}

TEST(import_title_key_fallback_and_both_wrong) {
    Fixture fx;
    auto s = fx.spec();
    s.rights = true;
    auto g = synth::game(fx.keys, s, fx.rng);
    // a ticket with a wrong key: title.keys' is tried next and works
    for (auto &f : g.package)
        if (f.first.size() > 4 && f.first.compare(f.first.size() - 4, 4, ".tik") == 0) f.second[4 + 0x100 + 0x3C + 0x40] ^= 0x80;
    {
        Importer imp(fx.input(g.package, PackageKind::NSP, title_keys_line(g, fx.keys, 0)));
        fx.check_extract(imp);
    }
    std::string e = ERROR_OF(Importer(fx.input(g.package, PackageKind::NSP, title_keys_line(g, fx.keys, 0, true))));
    CHECK(contains(e, "doesn't decrypt"));
    CHECK(contains(e, "ticket, title.keys"));
    CHECK(!contains(e, hex(g.title_key.data(), 16)));
}

TEST(import_personalized_ticket_ignored) {
    Fixture fx;
    auto s = fx.spec();
    s.rights = true;
    auto g = synth::game(fx.keys, s, fx.rng);
    for (auto &f : g.package)
        if (f.first.size() > 4 && f.first.compare(f.first.size() - 4, 4, ".tik") == 0) f.second[4 + 0x100 + 0x3C + 0x141] = 1;
    CHECK(contains(ERROR_OF(Importer(fx.input(g.package))), "no title key"));
}

TEST(import_refuses_other_games_and_missing_content) {
    Fixture fx;
    auto s = fx.spec();
    s.title_id = 0x0100000000010000ull;
    auto other = synth::game(fx.keys, s, fx.rng);
    std::string e = ERROR_OF(Importer(fx.input(other.package)));
    CHECK(contains(e, "is not Tears of the Kingdom"));
    CHECK(contains(e, "0100000000010000"));
    CHECK(contains(e, "não é o Tears of the Kingdom"));

    auto g = synth::game(fx.keys, fx.spec(), fx.rng);
    synth::Files no_program(g.package.begin() + 1, g.package.end());
    CHECK(contains(ERROR_OF(Importer(fx.input(no_program))), "is missing from the package"));
    synth::Files no_meta = {g.package[0]};
    CHECK(contains(ERROR_OF(Importer(fx.input(no_meta))), "it holds: none"));
}

TEST(import_wrong_header_key) {
    Fixture fx;
    auto g = synth::game(fx.keys, fx.spec(), fx.rng);
    auto in = fx.input(g.package);
    std::string k = fx.keys.prod_keys();
    k[k.find("header_key = ") + 13] ^= 1;
    in.prod_keys = text(k, "prod.keys");
    CHECK(contains(ERROR_OF(Importer{in}), "header_key"));
}

TEST(import_missing_key_area_key) {
    Fixture fx;
    auto s = fx.spec();
    s.keygen = 12;  // (revision 0x0b: not in the test keys)
    auto g = synth::game(fx.keys, s, fx.rng);
    std::string e = ERROR_OF(Importer(fx.input(g.package)));
    CHECK(contains(e, "key_area_key_application_0b"));
}

TEST(import_cancel) {
    Fixture fx;
    auto g = synth::game(fx.keys, fx.spec(), fx.rng);
    Importer imp(fx.input(g.package));
    std::atomic<bool> cancel{true};
    std::string dir = temp_dir();
    bool cancelled = false;
    try {
        imp.extract_to(dir + "/x", nullptr, &cancel);
    } catch (const Cancelled &) {
        cancelled = true;
    }
    CHECK(cancelled);
    rm_tree(dir);
}

// nximport_tests --write-sample <dir> [romfs MiB]: a synthetic base.nsp, an update.nsp patching it (BKTR) and their
// test prod.keys on disk, for the CLI and verify.sh by hand (random test keys: nothing of Nintendo's)
static bool write_bytes(const std::string &path, const void *p, size_t n) {
    FILE *f = fopen(path.c_str(), "wb");
    bool ok = f && fwrite(p, 1, n, f) == n;
    return f && fclose(f) == 0 && ok;
}

int write_sample(const std::string &dir, int mib) {
    Fixture fx;
    for (int i = 0; i < mib; i++) fx.romfs.push_back({"Pack/Big" + std::to_string(i) + ".pack", synth::random_bytes(fx.rng, (1u << 20) - 13 * i)});
    auto g = synth::game(fx.keys, fx.spec(), fx.rng);
    synth::GameSpec u = fx.spec();
    u.title_id = GAME_PATCH_ID, u.meta_type = 0x81, u.version = 786432, u.rights = true, u.keygen = 3;
    u.exefs = synth::sample_exefs(fx.rng);
    u.romfs[0].second = {'1', '.', '6', '.', '0'};
    for (size_t i = 0; i < u.romfs[1].second.size(); i += 4096) u.romfs[1].second[i] ^= 0x33;
    u.romfs.push_back({"Pack/New.pack", synth::random_bytes(fx.rng, 77777)});
    u.base_romfs = &fx.romfs;
    auto up = synth::game(fx.keys, u, fx.rng);
    synth::Bytes nsp = synth::pfs0(g.package), unsp = synth::pfs0(up.package);
    std::string k = fx.keys.prod_keys();
    return write_bytes(dir + "/base.nsp", nsp.data(), nsp.size()) && write_bytes(dir + "/update.nsp", unsp.data(), unsp.size()) &&
                   write_bytes(dir + "/prod.keys", k.data(), k.size())
               ? 0
               : 1;
}
