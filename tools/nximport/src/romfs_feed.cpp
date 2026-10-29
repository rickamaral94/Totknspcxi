#include "romfs_feed.h"

#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nx {

enum : uint64_t { FEED_CHUNK = 1u << 20 };

RomfsFeed::RomfsFeed(const Importer &imp, const std::atomic<bool> *cancel) : imp_(imp), cancel_(cancel) {}

RomfsFeed::~RomfsFeed() { finish(); }

const Error *RomfsFeed::finish() {
    if (producer_.joinable()) producer_.join();
    return error_.get();
}

int RomfsFeed::open_fd(void *ctx, int index) {
    auto *f = static_cast<RomfsFeed *>(ctx);
    f->finish();
    if (f->error_ || (f->cancel_ && f->cancel_->load())) {
        if (!f->error_) f->error_.reset(new Cancelled());
        return -1;
    }
    if (index < 0 || size_t(index) >= f->imp_.romfs().files.size()) return -1;
    f->attempt_ = index == f->last_index_ ? f->attempt_ + 1 : 0;
    f->last_index_ = index;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sv) != 0) {
        std::string e = strerror(errno);
        f->error_.reset(new Error("socketpair failed (" + e + ")", "socketpair falhou (" + e + ")"));
        return -1;
    }
    shutdown(sv[0], SHUT_WR);  // (the builder's end only reads)
    f->producer_ = std::thread(&RomfsFeed::produce, f, index, sv[1], f->attempt_);
    return sv[0];
}

void RomfsFeed::produce(int index, int fd, int attempt) {
    const uint64_t size = imp_.romfs().files[size_t(index)].size;
    std::vector<uint8_t> buf(size_t(std::min<uint64_t>(FEED_CHUNK, size ? size : 1)));
    bool stop = false;
    try {
        for (uint64_t off = 0; off < size && !stop;) {
            if (cancel_ && cancel_->load()) throw Cancelled();
            uint64_t n = std::min<uint64_t>(size - off, buf.size());
            if (index == faults.file && off + n > faults.bytes) {
                n = faults.bytes > off ? faults.bytes - off : 0;
                stop = true;
                if (!faults.transient)
                    error_.reset(new Error("test fault in " + imp_.romfs().files[size_t(index)].path, "falha de teste"));
                else if (attempt > 0)
                    n = std::min<uint64_t>(size - off, buf.size()), stop = false;  // (the retry goes through)
            }
            imp_.read_romfs(size_t(index), buf.data(), n, off);
            for (uint64_t sent = 0; sent < n;) {
                // (a bounded wait: a cancel is seen within 100 ms even while the builder isn't reading)
                pollfd p{fd, POLLOUT, 0};
                int r = poll(&p, 1, 100);
                if (cancel_ && cancel_->load()) throw Cancelled();
                if (r <= 0) continue;
                ssize_t w = send(fd, buf.data() + sent, size_t(n - sent), MSG_NOSIGNAL);
                if (w < 0 && (errno == EINTR || errno == EAGAIN)) continue;
                if (w < 0) {  // EPIPE / ECONNRESET: the builder closed its end (its own failure, reported by it)
                    stop = true;
                    break;
                }
                sent += uint64_t(w);
            }
            off += n;
        }
    } catch (const Cancelled &) {
        error_.reset(new Cancelled());
    } catch (const Error &e) {
        error_.reset(new Error(e));
    }
    close(fd);  // (EOF for the builder: whole, or short after a failure)
}

}  // namespace nx
