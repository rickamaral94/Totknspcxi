// The compatibility proof for the app's path (docs/IMPORT-NSP-XCI.md, Fase 3): runtime/romfs_build.c, unchanged,
// building romfs.bin through RomfsFeed's socketpairs must give the very bytes it gives from the same files extracted
// to a folder; and a producer failure, a transient short read, a cancel and a consumer that stops reading must all
// end cleanly (no hang, threads joined, the cause reported).
#include "romfs_build.h"
#include "romfs_feed.h"
#include "synth.h"
#include "test.h"

#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

using namespace nx;

std::string temp_dir();
void rm_tree(const std::string &p);
synth::Bytes read_file(const std::string &p);

namespace {

struct FeedFixture {
    std::mt19937_64 rng{77};
    synth::Keys keys{rng};
    synth::Files exefs = synth::sample_exefs(rng), romfs = synth::sample_romfs(rng);
    std::unique_ptr<Importer> imp;
    std::string dir = temp_dir();
    std::vector<RomfsEntry> entries;
    FeedFixture() {
        for (int i = 0; i < 6; i++) romfs.push_back({"Pack/Big" + std::to_string(i) + ".pack", synth::random_bytes(rng, 3000000 + 77 * i)});
        synth::GameSpec s;
        s.exefs = exefs, s.romfs = romfs;
        auto g = synth::game(keys, s, rng);
        ImportInput in;
        in.base = std::make_shared<MemReader>(synth::pfs0(g.package), "base.nsp");
        std::string k = keys.prod_keys();
        in.prod_keys = std::make_shared<MemReader>(synth::Bytes(k.begin(), k.end()), "prod.keys");
        imp.reset(new Importer(in));
        for (auto &f : imp->romfs().files) entries.push_back({f.path.c_str(), f.size});
    }
    ~FeedFixture() { rm_tree(dir); }
    // romfs.bin through the feed; why: the builder's message
    bool build(RomfsFeed &feed, const std::string &out, char *why, size_t n,
               void (*progress)(uint64_t, uint64_t) = nullptr) {
        return romfs_build_entries(entries.data(), int(entries.size()), &RomfsFeed::open_fd, &feed, out.c_str(), nullptr,
                                   nullptr, progress, why, n);
    }
};

std::atomic<bool> *g_cancel_at_progress;
void cancel_halfway(uint64_t done, uint64_t total) {
    if (done > total / 2) g_cancel_at_progress->store(true);
}

}  // namespace

TEST(feed_matches_folder_build) {
    FeedFixture fx;
    // the reference: the files extracted to a folder, the image built from it (the app's current path)
    fx.imp->extract_to(fx.dir + "/x", nullptr, nullptr);
    char why[256] = "";
    REQUIRE(romfs_build((fx.dir + "/x/romfs").c_str(), (fx.dir + "/folder.bin").c_str(), nullptr, why, sizeof why));
    RomfsFeed feed(*fx.imp, nullptr);
    REQUIRE(fx.build(feed, fx.dir + "/feed.bin", why, sizeof why));
    CHECK(feed.finish() == nullptr);
    synth::Bytes a = read_file(fx.dir + "/folder.bin"), b = read_file(fx.dir + "/feed.bin");
    CHECK(a.size() > 18000000);
    CHECK(a == b);
    CHECK(romfs_image_ok((fx.dir + "/feed.bin").c_str(), why, sizeof why));
    // and the runtime's own check of the set, as the app runs it before a setup
    std::vector<const char *> paths;
    for (auto &e : fx.entries) paths.push_back(e.path);
    std::string vt = fx.imp->version_txt();
    CHECK(romfs_check_set(100, paths.data(), int(paths.size()), vt.c_str(), why, sizeof why) != ROMFS_SET_OTHER_VERSION);
}

TEST(feed_producer_error_stops_the_build) {
    FeedFixture fx;
    RomfsFeed feed(*fx.imp, nullptr);
    for (size_t i = 0; i < fx.entries.size(); i++)
        if (fx.entries[i].size > 2000000) feed.faults.file = int(i);  // (a large file: the fault falls inside it)
    REQUIRE(feed.faults.file >= 0);
    feed.faults.bytes = 1500000;
    char why[256] = "";
    CHECK(!fx.build(feed, fx.dir + "/f.bin", why, sizeof why));
    const Error *e = feed.finish();
    REQUIRE(e);
    CHECK(contains(e->en(), "test fault in"));
    CHECK(contains(why, "can't read"));
}

TEST(feed_transient_short_read_is_retried) {
    FeedFixture fx;
    RomfsFeed feed(*fx.imp, nullptr);
    feed.faults.file = 3;
    feed.faults.bytes = 1;
    feed.faults.transient = true;
    char why[256] = "";
    REQUIRE(fx.build(feed, fx.dir + "/t.bin", why, sizeof why));
    CHECK(feed.finish() == nullptr);
    RomfsFeed clean(*fx.imp, nullptr);
    REQUIRE(fx.build(clean, fx.dir + "/c.bin", why, sizeof why));
    CHECK(read_file(fx.dir + "/t.bin") == read_file(fx.dir + "/c.bin"));
}

TEST(feed_cancel_midway) {
    FeedFixture fx;
    std::atomic<bool> cancel{false};
    g_cancel_at_progress = &cancel;
    RomfsFeed feed(*fx.imp, &cancel);
    char why[256] = "";
    CHECK(!fx.build(feed, fx.dir + "/k.bin", why, sizeof why, cancel_halfway));
    const Error *e = feed.finish();
    REQUIRE(e);
    CHECK(dynamic_cast<const Cancelled *>(e) != nullptr);
}

TEST(feed_consumer_stops_reading) {
    // the builder closing its end mid-file (its own write failed): the producer gets EPIPE, ends, joins; not a feed error
    FeedFixture fx;
    RomfsFeed feed(*fx.imp, nullptr);
    int big = 0;
    for (size_t i = 0; i < fx.entries.size(); i++)
        if (fx.entries[i].size > 2000000) big = int(i);
    int fd = RomfsFeed::open_fd(&feed, big);
    REQUIRE(fd >= 0);
    char b[4096];
    CHECK(read(fd, b, sizeof b) > 0);
    close(fd);
    CHECK(feed.finish() == nullptr);
    // and the next file still opens
    int fd2 = RomfsFeed::open_fd(&feed, 0);
    CHECK(fd2 >= 0);
    std::string data;
    for (ssize_t r; (r = read(fd2, b, sizeof b)) > 0;) data.append(b, size_t(r));
    close(fd2);
    CHECK(data.size() == fx.entries[0].size);
    CHECK(feed.finish() == nullptr);
}
