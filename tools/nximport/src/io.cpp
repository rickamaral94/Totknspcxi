#include "io.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace nx {

std::string hex(const uint8_t *p, size_t n) {
    static const char d[] = "0123456789abcdef";
    std::string s(n * 2, '0');
    for (size_t i = 0; i < n; i++) s[2 * i] = d[p[i] >> 4], s[2 * i + 1] = d[p[i] & 15];
    return s;
}

bool FdReader::seekable(int fd, uint64_t *size_out) {
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0) return false;
    uint8_t b;
    if (pread(fd, &b, 1, 0) != 1 || pread(fd, &b, 1, st.st_size - 1) != 1) return false;
    if (size_out) *size_out = uint64_t(st.st_size);
    return true;
}

FdReader::FdReader(int fd, bool owns, std::string what) : Reader(std::move(what)), fd_(fd), owns_(owns), size_(0) {
    if (!seekable(fd, &size_)) {
        if (owns_) close(fd_);
        fail(this->what() + ": can't be read at random offsets (choose a file stored on the device)",
             this->what() + ": não pode ser lido em posições aleatórias (escolha um arquivo guardado no aparelho)");
    }
}

FdReader::~FdReader() {
    if (owns_) close(fd_);
}

void FdReader::read_impl(void *dst, uint64_t n, uint64_t off) const {
    auto *p = static_cast<uint8_t *>(dst);
    while (n) {
        size_t want = n > (1u << 30) ? (1u << 30) : size_t(n);
        ssize_t r = pread(fd_, p, want, off_t(off));
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) {
            std::string e = r < 0 ? strerror(errno) : "end of file";
            fail(what() + ": read error (" + e + ")", what() + ": erro de leitura (" + e + ")");
        }
        p += r, off += uint64_t(r), n -= uint64_t(r);
    }
}

ReaderPtr open_file(const std::string &path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        std::string e = strerror(errno);
        fail("Can't open " + path + " (" + e + ")", "Não foi possível abrir " + path + " (" + e + ")");
    }
    size_t slash = path.find_last_of('/');
    return std::make_shared<FdReader>(fd, true, slash == std::string::npos ? path : path.substr(slash + 1));
}

SubReader::SubReader(ReaderPtr parent, uint64_t base, uint64_t size, std::string what)
    : Reader(std::move(what)), parent_(std::move(parent)), base_(base), size_(size) {
    check_range(base, size, parent_->size(), this->what().c_str());
}

void MemReader::read_impl(void *dst, uint64_t n, uint64_t off) const { memcpy(dst, data_.data() + off, size_t(n)); }

}  // namespace nx
