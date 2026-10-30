// Offsets past 4 GiB (a whole game's RomFS is ~13 GB): the descriptor reader on a sparse file, AES-CTR against
// counters computed independently (ECB of the expected counter block), and an IVFC tree over 4.3 GiB of data read on
// both sides of 2^32. The data is procedural (a function of the offset): nothing that size is held in memory.
#include "crypto.h"
#include "integrity.h"
#include "nca.h"
#include "test.h"

#include <cstring>
#include <fcntl.h>
#include <unistd.h>

using namespace nx;

std::string temp_dir();
void rm_tree(const std::string &p);

namespace {

uint8_t pattern_byte(uint64_t off) {
    uint64_t z = (off >> 3) + 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return uint8_t(z >> (8 * (off & 7)));
}

class PatternReader : public Reader {
public:
    explicit PatternReader(uint64_t size) : Reader("pattern"), size_(size) {}
    uint64_t size() const override { return size_; }

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override {
        auto *p = static_cast<uint8_t *>(dst);
        for (uint64_t i = 0; i < n; i++) p[i] = pattern_byte(off + i);
    }

private:
    uint64_t size_;
};

// [head bytes in memory][tail from another reader at tail_off]
class JoinReader : public Reader {
public:
    JoinReader(std::vector<uint8_t> head, uint64_t tail_off, ReaderPtr tail)
        : Reader("join"), head_(std::move(head)), tail_off_(tail_off), tail_(std::move(tail)) {}
    uint64_t size() const override { return tail_off_ + tail_->size(); }

protected:
    void read_impl(void *dst, uint64_t n, uint64_t off) const override {
        auto *p = static_cast<uint8_t *>(dst);
        while (n) {
            if (off < tail_off_) {
                uint64_t k = std::min(n, tail_off_ - off);
                for (uint64_t i = 0; i < k; i++) p[i] = off + i < head_.size() ? head_[off + i] : 0;
                p += k, off += k, n -= k;
            } else {
                tail_->read(p, n, off - tail_off_);
                return;
            }
        }
    }

private:
    std::vector<uint8_t> head_;
    uint64_t tail_off_;
    ReaderPtr tail_;
};

const uint64_t G4 = 1ull << 32;

}  // namespace

TEST(big_fd_reader_past_4gib) {
    std::string dir = temp_dir(), p = dir + "/sparse.bin";
    int fd = open(p.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    REQUIRE(fd >= 0);
    REQUIRE(ftruncate(fd, off_t(G4 + (512ull << 20))) == 0);
    const char mark[] = "past-4GiB-marker";
    for (uint64_t at : std::initializer_list<uint64_t>{G4 - 7, G4 + 2850816, G4 + (500ull << 20)})
        REQUIRE(pwrite(fd, mark, sizeof mark, off_t(at)) == ssize_t(sizeof mark));
    close(fd);
    ReaderPtr r = open_file(p);
    CHECK(r->size() == G4 + (512ull << 20));
    for (uint64_t at : std::initializer_list<uint64_t>{G4 - 7, G4 + 2850816, G4 + (500ull << 20)}) {
        char got[sizeof mark] = {};
        r->read(got, sizeof got, at);
        CHECK(!memcmp(got, mark, sizeof mark));
        SubReader sub(r, 4096, r->size() - 4096, "sub");
        memset(got, 0, sizeof got);
        sub.read(got, sizeof got, at - 4096);
        CHECK(!memcmp(got, mark, sizeof mark));
    }
    rm_tree(dir);
}

TEST(big_ctr_past_4gib) {
    Key128 key;
    for (int i = 0; i < 16; i++) key.b[i] = uint8_t(i * 17 + 3);
    key.set = true;
    const uint8_t upper[8] = {0, 0, 0, 1, 0x12, 0x34, 0x56, 0x78};
    const uint64_t section_off = 0xC00 + 0x4000;  // (where a section starts in its NCA)
    auto plain = std::make_shared<PatternReader>(G4 + (64ull << 20));
    CtrReader r(plain, key, upper, section_off, "ctr");
    for (uint64_t at : std::initializer_list<uint64_t>{G4 - 4096 - 5, G4 - 16, G4 + 2850816 + 3, G4 + (32ull << 20)}) {
        const uint64_t n = 8192 + 7;
        std::vector<uint8_t> got(n);
        r.read(got.data(), n, at);
        // expected: plain XOR AES-ECB(upper || BE64((section_off + offset) >> 4)), block by block
        for (uint64_t i = 0; i < n; i++) {
            uint64_t o = at + i, blk = (section_off + o) >> 4;
            uint8_t ctr[16], ks[16];
            memcpy(ctr, upper, 8);
            for (int k = 0; k < 8; k++) ctr[15 - k] = uint8_t(blk >> (8 * k));
            aes_ecb_encrypt(key, ctr, ks, 16);
            uint8_t want = uint8_t(pattern_byte(o) ^ ks[(section_off + o) & 15]);
            if (got[i] != want) {
                fprintf(stderr, "  CTR differs at %llu\n", (unsigned long long)o);
                CHECK(false);
                break;
            }
        }
    }
}

TEST(big_ivfc_past_4gib) {
    // level 6: 4 GiB + 3 MiB + a partial block of pattern data, 16 KiB blocks (as games have them)
    const uint32_t log2 = 14, bs = 1u << log2;
    const uint64_t data_size = G4 + (3ull << 20) + 1234;
    auto data = std::make_shared<PatternReader>(data_size);
    std::vector<uint8_t> levels[6];
    {  // level 5: the data's block hashes (each block made and hashed here, independently of IvfcReader)
        uint64_t blocks = (data_size + bs - 1) / bs;
        levels[4].resize(blocks * 32);
        std::vector<uint8_t> b(bs);
        for (uint64_t i = 0; i < blocks; i++) {
            uint64_t n = std::min<uint64_t>(bs, data_size - i * bs);
            data->read(b.data(), n, i * bs);
            if (n < bs) memset(b.data() + n, 0, bs - n);
            sha256(b.data(), bs, &levels[4][i * 32]);
        }
    }
    for (int l = 3; l >= 0; l--) {
        const std::vector<uint8_t> &below = levels[l + 1];
        uint64_t blocks = (below.size() + bs - 1) / bs;
        levels[l].resize(blocks * 32);
        std::vector<uint8_t> b(bs);
        for (uint64_t i = 0; i < blocks; i++) {
            uint64_t n = std::min<uint64_t>(bs, below.size() - i * bs);
            memcpy(b.data(), below.data() + i * bs, n);
            memset(b.data() + n, 0, bs - n);
            sha256(b.data(), bs, &levels[l][i * 32]);
        }
    }
    std::vector<uint8_t> head;
    uint8_t hd[0xF8] = {};
    memcpy(hd, "IVFC", 4);
    auto put32 = [](uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (8 * i)); };
    auto put64 = [](uint8_t *p, uint64_t v) { for (int i = 0; i < 8; i++) p[i] = uint8_t(v >> (8 * i)); };
    put32(hd + 4, 0x20000), put32(hd + 8, 0x20), put32(hd + 12, 7);
    for (int l = 0; l < 5; l++) {
        uint64_t off = (head.size() + bs - 1) / bs * bs;
        head.resize(off);
        head.insert(head.end(), levels[l].begin(), levels[l].end());
        put64(hd + 0x10 + 0x18 * l, off), put64(hd + 0x18 + 0x18 * l, levels[l].size()), put32(hd + 0x20 + 0x18 * l, log2);
    }
    uint64_t data_off = (head.size() + bs - 1) / bs * bs;
    put64(hd + 0x10 + 0x18 * 5, data_off), put64(hd + 0x18 + 0x18 * 5, data_size), put32(hd + 0x20 + 0x18 * 5, log2);
    {
        std::vector<uint8_t> b(bs, 0);
        memcpy(b.data(), levels[0].data(), levels[0].size());
        sha256(b.data(), bs, hd + 0xC0);
    }
    auto storage = std::make_shared<JoinReader>(head, data_off, data);
    IvfcReader ivfc(storage, hd, "big");
    CHECK(ivfc.size() == data_size);
    for (uint64_t at : std::initializer_list<uint64_t>{G4 - bs - 3, G4 - 1, G4 + 2850816, G4 + 2850816 - 100, data_size - 5000}) {
        uint64_t n = std::min<uint64_t>(3 * bs + 11, data_size - at);
        std::vector<uint8_t> got(n);
        ivfc.read(got.data(), n, at);
        bool same = true;
        for (uint64_t i = 0; i < n && same; i++) same = got[i] == pattern_byte(at + i);
        if (!same) fprintf(stderr, "  IVFC data differs near %llu\n", (unsigned long long)at);
        CHECK(same);
    }
}
