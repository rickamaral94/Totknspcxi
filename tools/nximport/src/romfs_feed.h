// The imported RomFS's files as descriptors for runtime/romfs_build.c's romfs_build_entries (the app's romfs.bin is
// built straight from the packages: no romfs/ folder, one pass of writes). Its contract, at upstream 4acbf40:
// open_fd(ctx, i) hands over a descriptor it fdopen()s and fread()s sequentially for exactly entries[i].size bytes,
// then wants EOF; no seek, no fstat; one file at a time, one thread; a short read is retried twice from byte 0 with a
// new open_fd; it closes the descriptor; it has no cancel hook.
//
// So: open_fd returns the read end of a socketpair (AF_UNIX stream: send(MSG_NOSIGNAL) gets EPIPE instead of a
// process-wide SIGPIPE) and a producer thread writes file i into the other end, then closes it. Before it starts one,
// open_fd joins the previous producer. A failure in the producer (a read error, a failed hash, cancel) closes its end
// early: the builder sees a short read; open_fd then refuses (-1) so the builder stops, and error() says why.
#pragma once
#include "nximport.h"

#include <atomic>
#include <thread>

namespace nx {

class RomfsFeed {
public:
    RomfsFeed(const Importer &imp, const std::atomic<bool> *cancel);
    ~RomfsFeed();  // joins the producer

    // romfs_build_entries' callback: ctx is the RomfsFeed
    static int open_fd(void *ctx, int index);

    // Waits for the last producer; the first failure, if any (null: none). A consumer that stopped reading (the
    // builder failing on its side) is not a failure of the feed.
    const Error *finish();

    // (tests) a producer that stops after `bytes` of file `file`: as an error, or silently on its first attempt only
    // (a transient short read, which the builder's retry must get over)
    struct Faults {
        int file = -1;
        uint64_t bytes = 0;
        bool transient = false;
    } faults;

private:
    void produce(int index, int fd, int attempt);

    const Importer &imp_;
    const std::atomic<bool> *cancel_;
    std::thread producer_;
    std::unique_ptr<Error> error_;
    int last_index_ = -1, attempt_ = 0;
};

}  // namespace nx
