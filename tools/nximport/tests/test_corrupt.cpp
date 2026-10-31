// Corruption: deterministic bit flips and truncations over whole synthetic packages. Each case must either fail with
// an nx::Error or extract exactly the original files (a flip in bytes nothing reads, such as a signature) - never
// crash, hang, read out of bounds or produce different files. Run under ASan/UBSan (NXIMPORT_SANITIZE).
#include "nximport.h"
#include "synth.h"
#include "test.h"

#include <cstring>
#include <sys/stat.h>

using namespace nx;

std::string temp_dir();
void rm_tree(const std::string &p);
synth::Bytes read_file(const std::string &p);

namespace {

struct Case {
    std::mt19937_64 rng{7};
    synth::Keys keys{rng};
    synth::Files exefs = synth::sample_exefs(rng), romfs = synth::sample_romfs(rng);
    synth::Bytes nsp;
    std::string dir = temp_dir();
    Case(bool rights) {
        synth::GameSpec s;
        s.exefs = exefs, s.romfs = romfs, s.rights = rights;
        nsp = synth::pfs0(synth::game(keys, s, rng).package);
    }
    ~Case() { rm_tree(dir); }

    // 0: an Error, 1: the original files, -1: different files (a failure)
    int run(const synth::Bytes &package) {
        ImportInput in;
        in.base = std::make_shared<MemReader>(package, "base.nsp");
        std::string k = keys.prod_keys();
        in.prod_keys = std::make_shared<MemReader>(synth::Bytes(k.begin(), k.end()), "prod.keys");
        std::string out = dir + "/o";
        rm_tree(out);
        try {
            Importer imp(in);
            imp.extract_to(out, nullptr, nullptr);
        } catch (const Error &) {
            return 0;
        }
        for (auto &f : exefs)
            if (read_file(out + "/exefs/" + f.first) != f.second) return -1;
        for (auto &f : romfs)
            if (read_file(out + "/romfs/" + f.first) != f.second) return -1;
        return 1;
    }
};

}  // namespace

static void flips(bool rights) {
    Case c(rights);
    REQUIRE(c.run(c.nsp) == 1);
    std::mt19937_64 pick(99);
    int errors = 0, same = 0;
    const int N = 1500;
    for (int i = 0; i < N; i++) {
        synth::Bytes b = c.nsp;
        size_t at = i < 600 ? size_t(i) * 7 % 0x2000 : size_t(pick() % b.size());  // (headers densely, then anywhere)
        b[at] ^= uint8_t(1u << (pick() % 8));
        int r = c.run(b);
        if (r < 0) fprintf(stderr, "  flip at 0x%zx extracted different files\n", at);
        CHECK(r >= 0);
        (r == 0 ? errors : same)++;
    }
    fprintf(stderr, "  %d flips: %d refused, %d harmless\n", N, errors, same);
    CHECK(errors > N / 2);
}

TEST(corrupt_bit_flips) { flips(false); }
TEST(corrupt_bit_flips_title_key) { flips(true); }

TEST(corrupt_truncation) {
    Case c(false);
    for (size_t n = 0; n < c.nsp.size(); n += 1 + n / 8) {
        synth::Bytes b(c.nsp.begin(), c.nsp.begin() + n);
        CHECK(c.run(b) == 0);
    }
}

namespace {

// A package whose bytes at [at, at + 1) come back wrong: on the first read only (flaky storage), or differently on
// every read (unreliable storage)
class FlakyReader : public Reader {
public:
    FlakyReader(synth::Bytes b, uint64_t at, bool every_time)
        : Reader("base.nsp"), b_(std::move(b)), at_(at), every_(every_time) {}
    uint64_t size() const override { return b_.size(); }
    mutable int hits = 0;

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override {
        memcpy(dst, b_.data() + off, size_t(n));
        if (at_ >= off && at_ < off + n && (every_ || hits == 0))
            static_cast<uint8_t *>(dst)[at_ - off] ^= uint8_t(1 + hits++);
    }

private:
    synth::Bytes b_;
    uint64_t at_;
    bool every_;
};

}  // namespace

TEST(flaky_read_recovered_by_reread) {
    Case c(false);
    std::string k = c.keys.prod_keys(), out = c.dir + "/flaky";
    auto run = [&](uint64_t at, bool every, std::shared_ptr<FlakyReader> *keep) {
        ImportInput in;
        auto flaky = std::make_shared<FlakyReader>(c.nsp, at, every);
        in.base = flaky;
        in.prod_keys = std::make_shared<MemReader>(synth::Bytes(k.begin(), k.end()), "prod.keys");
        rm_tree(out);
        if (keep) *keep = flaky;
        return ERROR_OF(Importer(in).extract_to(out, nullptr, nullptr));
    };
    // a byte the RomFS hash tree covers: the first offset (past the headers) where wrong bytes every time fail it
    uint64_t at = 0;
    for (uint64_t o = 0x8000; o < c.nsp.size() && !at; o += 997)
        if (contains(run(o, true, nullptr), "different bytes each time")) at = o;
    REQUIRE(at);
    CHECK(contains(run(at, true, nullptr), "bytes diferentes a cada vez"));
    // wrong on the first read only: read again, the right bytes, the files as they were
    std::shared_ptr<FlakyReader> flaky;
    CHECK(run(at, false, &flaky).empty());
    CHECK(flaky->hits == 1);
    for (auto &f : c.romfs) CHECK(read_file(out + "/romfs/" + f.first) == f.second);
    for (auto &f : c.exefs) CHECK(read_file(out + "/exefs/" + f.first) == f.second);
}
