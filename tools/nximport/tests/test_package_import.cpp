// The app's transactional import (android/totkrecompandroid/app/src/main/cpp/package_import.cpp) on the host, with
// runtime/romfs_build.c as it is and small stand-ins for runtime/paths.c and the loader (the same rules: version roots,
// only the app's own game file names removed). A cancel, a failure or a kill before COMPLETE must leave an earlier
// setup as it was; a kill during the renames must be finished by package_import_recover.
#include "package_import.h"
#include "romfs_build.h"
#include "synth.h"
#include "test.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace nx;

std::string temp_dir();
void rm_tree(const std::string &p);
synth::Bytes read_file(const std::string &p);

// ---- stand-ins (runtime/paths.c, runtime/loader.c)
static std::string g_root;
static uint8_t g_build[8];
extern "C" bool rt_version_root(int v, char *out, size_t n) {
    if (v == 143) return (size_t)snprintf(out, n, "%s", g_root.c_str()) < n;  // (GAME_V_MAIN: the data root itself)
    return false;
}
static const char *const GAME_FILES[] = {"exefs", "romfs", "romfs.bin", "romfs.link", "totkgame.so", "totkgame.so.id"};
extern "C" bool rt_remove_game_file(const char *dir, const char *name) {
    bool known = false;
    for (const char *g : GAME_FILES) known |= !strcmp(g, name);
    if (!known) return false;
    rm_tree(std::string(dir) + "/" + name);
    return true;
}
extern "C" bool rt_remove_game_files_for_setup(const char *dir) {
    for (const char *g : GAME_FILES) rt_remove_game_file(dir, g);
    return true;
}
extern "C" int game_version_of_build(const uint8_t id[8]) { return memcmp(id, g_build, 8) ? 0 : 143; }

namespace {

bool keep_movies(const char *rel) { return !strncmp(rel, "Movie/", 6); }
// (the synthetic RomFS has a dozen files, not a whole game's: the runtime's check would refuse it - pkg_import_refusals)
int any_set(int, const char *const *, int, const char *, char *, size_t) { return ROMFS_SET_OK; }
PackageImportHooks hooks() {
    PackageImportHooks h;
    h.keep_loose = keep_movies;
    h.check_set = any_set;
    return h;
}

void put(const std::string &p, const std::string &s) {
    FILE *f = fopen(p.c_str(), "wb");
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
}
std::string get(const std::string &p) {
    auto b = read_file(p);
    return std::string(b.begin(), b.end());
}
bool exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

struct PkgFixture {
    std::mt19937_64 rng{2024};
    synth::Keys keys{rng};
    synth::Files exefs = synth::sample_exefs(rng), romfs = synth::sample_romfs(rng);
    std::string dir = temp_dir(), nsp, prod;
    synth::Bytes nsp_bytes;
    PkgFixture() {
        g_root = dir + "/data";
        mkdir(g_root.c_str(), 0755);
        for (int i = 0; i < 4; i++) romfs.push_back({"Pack/Big" + std::to_string(i) + ".pack", synth::random_bytes(rng, 2500000)});
        memcpy(g_build, exefs[0].second.data() + 0x40, 8);  // (the synthetic main's build id is version 143 (GAME_V_143) here)
        synth::GameSpec s;
        s.exefs = exefs, s.romfs = romfs;
        nsp_bytes = synth::pfs0(synth::game(keys, s, rng).package);
        nsp = dir + "/base.nsp", prod = dir + "/prod.keys";
        put(nsp, std::string(nsp_bytes.begin(), nsp_bytes.end()));
        put(prod, keys.prod_keys());
    }
    ~PkgFixture() { rm_tree(dir); }
    PackageImportInputs inputs(const std::string &package = "") {
        PackageImportInputs in;
        in.base_fd = open((package.empty() ? nsp : package).c_str(), O_RDONLY);
        in.prod_keys_fd = open(prod.c_str(), O_RDONLY);
        return in;
    }
    void old_setup() {  // an earlier, working setup
        std::string d = g_root + "/extracted";
        mkdir(d.c_str(), 0755);
        mkdir((d + "/exefs").c_str(), 0755);
        put(d + "/exefs/main", "OLD MAIN");
        put(d + "/romfs.bin", "OLD IMAGE");
        put(d + "/totkgame.so", "OLD CODE");
        mkdir((d + "/.gamebuild").c_str(), 0755);
        put(d + "/.gamebuild/obj.o", "OBJECTS");
    }
    bool old_intact() {
        std::string d = g_root + "/extracted";
        return get(d + "/exefs/main") == "OLD MAIN" && get(d + "/romfs.bin") == "OLD IMAGE" && get(d + "/totkgame.so") == "OLD CODE";
    }
};

std::atomic<bool> *g_cancel_here;
uint64_t g_cancel_after;

}  // namespace

TEST(pkg_import_fresh_and_replacing) {
    PkgFixture fx;
    fx.old_setup();
    PackageImportHooks h = hooks();
    std::vector<std::string> steps;
    h.step = [&](const char *s) { steps.push_back(s); };
    uint64_t last = 0;
    h.progress = [&](uint64_t d, uint64_t) { CHECK(d >= last); last = d; };
    PackageImportOutcome o = package_import_run(fx.inputs(), h, nullptr);
    REQUIRE(o.result == PKG_DONE);
    CHECK(o.version == 143);
    std::string d = g_root + "/extracted";
    CHECK(o.dest == d);
    CHECK(steps.size() == 3);
    // the new files in place, the old ones gone (the compiled objects stay), no staging left
    for (auto &f : fx.exefs) CHECK(read_file(d + "/exefs/" + f.first) == f.second);
    CHECK(!exists(d + "/totkgame.so"));
    CHECK(get(d + "/.gamebuild/obj.o") == "OBJECTS");
    CHECK(!exists(g_root + "/extracted.importing"));
    CHECK(read_file(d + "/romfs/Movie/Demo101.mp4") == fx.romfs[6].second);
    CHECK(!exists(d + "/romfs/Pack"));
    // romfs.bin: the image the folder build makes from the same files
    std::string ref = fx.dir + "/ref";
    mkdir(ref.c_str(), 0755);
    for (auto &f : fx.romfs) {
        std::string p = ref + "/" + f.first;
        for (size_t s = p.find('/', ref.size() + 1); s != std::string::npos; s = p.find('/', s + 1)) mkdir(p.substr(0, s).c_str(), 0755);
        put(p, std::string(f.second.begin(), f.second.end()));
    }
    char why[256];
    REQUIRE(romfs_build(ref.c_str(), (fx.dir + "/ref.bin").c_str(), nullptr, why, sizeof why));
    CHECK(read_file(fx.dir + "/ref.bin") == read_file(d + "/romfs.bin"));
}

TEST(pkg_import_cancel_keeps_old_setup) {
    PkgFixture fx;
    fx.old_setup();
    std::atomic<bool> cancel{false};
    PackageImportHooks h = hooks();
    h.progress = [&](uint64_t d, uint64_t t) {
        if (d > t / 2) cancel = true;
    };
    PackageImportOutcome o = package_import_run(fx.inputs(), h, &cancel);
    CHECK(o.result == PKG_CANCELLED);
    CHECK(fx.old_intact());
    CHECK(!exists(g_root + "/extracted.importing"));
}

TEST(pkg_import_failure_keeps_old_setup) {
    PkgFixture fx;
    fx.old_setup();
    // a flipped byte deep in the RomFS data: found by the hash tree while romfs.bin is built
    synth::Bytes bad = fx.nsp_bytes;
    bad[bad.size() - 3000000] ^= 0x10;
    std::string p = fx.dir + "/bad.nsp";
    put(p, std::string(bad.begin(), bad.end()));
    PackageImportHooks h = hooks();
    PackageImportOutcome o = package_import_run(fx.inputs(p), h, nullptr);
    CHECK(o.result == PKG_UNUSABLE);
    CHECK(contains(o.detail, "integrity"));
    CHECK(fx.old_intact());
    CHECK(!exists(g_root + "/extracted.importing"));
    h.portuguese = true;
    o = package_import_run(fx.inputs(p), h, nullptr);
    CHECK(contains(o.detail, "integridade"));
}

TEST(pkg_import_refusals) {
    PkgFixture fx;
    {  // the runtime's own check of the files: a dozen files aren't a whole game
        PackageImportHooks d;
        PackageImportOutcome o = package_import_run(fx.inputs(), d, nullptr);
        CHECK(o.result == PKG_ROMFS_SET);
        CHECK(contains(o.detail, "missing"));
    }
    PackageImportHooks h = hooks();
    {  // a descriptor that can't seek (a provider that streams): said before anything, descriptors closed
        int pfd[2];
        REQUIRE(pipe(pfd) == 0);
        PackageImportInputs in = fx.inputs();
        close(in.base_fd);
        in.base_fd = pfd[0];
        PackageImportOutcome o = package_import_run(in, h, nullptr);
        CHECK(o.result == PKG_NOT_SEEKABLE);
        CHECK(o.slot == 0);
        void (*was)(int) = signal(SIGPIPE, SIG_IGN);  // (the read end was closed: EPIPE, not the signal)
        CHECK(write(pfd[1], "x", 1) < 0 && errno == EPIPE);
        signal(SIGPIPE, was);
        close(pfd[1]);
    }
    {  // a version this app doesn't run
        uint8_t saved[8];
        memcpy(saved, g_build, 8);
        g_build[0] ^= 1;
        CHECK(package_import_run(fx.inputs(), h, nullptr).result == PKG_VERSION);
        memcpy(g_build, saved, 8);
    }
    {  // keys that don't open it
        PackageImportInputs in = fx.inputs();
        close(in.prod_keys_fd);
        std::string k = fx.dir + "/wrong.keys";
        put(k, "header_key = " + std::string(64, '0') + "\n");
        in.prod_keys_fd = open(k.c_str(), O_RDONLY);
        PackageImportOutcome o = package_import_run(in, h, nullptr);
        CHECK(o.result == PKG_UNUSABLE);
        CHECK(contains(o.detail, "header_key"));
    }
    CHECK(!exists(g_root + "/extracted.importing"));
}

TEST(pkg_import_recovery) {
    PkgFixture fx;
    std::string st = g_root + "/extracted.importing", d = g_root + "/extracted";
    {  // unfinished: removed, the old setup untouched
        fx.old_setup();
        mkdir(st.c_str(), 0755);
        put(st + "/romfs.bin", "HALF");
        package_import_recover();
        CHECK(!exists(st));
        CHECK(fx.old_intact());
    }
    {  // complete, nothing moved yet: promoted
        mkdir(st.c_str(), 0755);
        mkdir((st + "/exefs").c_str(), 0755);
        put(st + "/exefs/main", "NEW MAIN");
        put(st + "/romfs.bin", "NEW IMAGE");
        put(st + "/COMPLETE", "143\n");
        package_import_recover();
        CHECK(!exists(st));
        CHECK(get(d + "/exefs/main") == "NEW MAIN");
        CHECK(get(d + "/romfs.bin") == "NEW IMAGE");
        CHECK(!exists(d + "/totkgame.so"));
    }
    {  // complete, killed after exefs/ was moved: the rest moved, the new exefs/ kept
        mkdir(st.c_str(), 0755);
        put(st + "/romfs.bin", "NEWER IMAGE");
        put(st + "/COMPLETE", "143\n");
        put(d + "/exefs/main", "NEWER MAIN");
        package_import_recover();
        CHECK(!exists(st));
        CHECK(get(d + "/exefs/main") == "NEWER MAIN");
        CHECK(get(d + "/romfs.bin") == "NEWER IMAGE");
    }
}

TEST(pkg_verify_packages) {
    PkgFixture fx;
    PackageImportHooks h = hooks();
    h.verify_only = true;
    uint64_t last = 0, total = 0;
    h.progress = [&](uint64_t d, uint64_t t) { last = d, total = t; };
    PackageImportOutcome o = package_import_run(fx.inputs(), h, nullptr);
    CHECK(o.result == PKG_DONE);
    CHECK(total > 10000000 && last == total);
    CHECK(!exists(g_root + "/extracted") && !exists(g_root + "/extracted.importing"));  // (nothing written)
    // a byte changed deep in the program NCA: named, as the package's own metadata disagrees with it
    synth::Bytes bad = fx.nsp_bytes;
    bad[bad.size() - 3000000] ^= 0x10;
    std::string p = fx.dir + "/bad.nsp";
    put(p, std::string(bad.begin(), bad.end()));
    o = package_import_run(fx.inputs(p), h, nullptr);
    CHECK(o.result == PKG_UNUSABLE);
    CHECK(contains(o.detail, ".nca: its SHA-256 doesn't match"));
}
