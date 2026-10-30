#include "package_import.h"

#include "nximport.h"
#include "romfs_feed.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <vector>

extern "C" {
#include "paths.h"
#include "romfs_build.h"
int game_version_of_build(const uint8_t id[8]);  // runtime/loader.c: 100, 160, 182, 190; 0 another one
}

static const char *const STAGING = "extracted.importing", *const COMPLETE = "COMPLETE";
static const char *const MOVED[] = {"exefs", "romfs", "romfs.bin"};  // (in this order; romfs.bin last)
static const int VERSIONS[] = {100, 160, 182, 190};

static bool exists(const std::string &p) {
    struct stat st;
    return lstat(p.c_str(), &st) == 0;
}

// The staging folder only (never anything outside it): what this import made
static void remove_staging_tree(const std::string &p) {
    struct stat st;
    if (lstat(p.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        if (DIR *d = opendir(p.c_str())) {
            while (struct dirent *e = readdir(d))
                if (strcmp(e->d_name, ".") && strcmp(e->d_name, "..")) remove_staging_tree(p + "/" + e->d_name);
            closedir(d);
        }
        rmdir(p.c_str());
    } else {
        unlink(p.c_str());
    }
}

static bool fsync_path(const std::string &p, bool dir) {
    int fd = open(p.c_str(), (dir ? O_RDONLY | O_DIRECTORY : O_RDONLY) | O_CLOEXEC);
    if (fd < 0) return false;
    bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
}

// The staged files into dest, the earlier setup's game files first removed (rt_remove_game_files_for_setup: they're
// the app's own; the compiled game code's objects stay). Each step can be done again: the start after a kill finishes it.
static bool promote(const std::string &root) {
    std::string staging = root + "/" + STAGING, dest = root + "/extracted";
    if (!exists(staging + "/" + COMPLETE)) return false;
    mkdir(dest.c_str(), 0755);
    if (exists(staging + "/exefs") && !rt_remove_game_files_for_setup(dest.c_str())) return false;  // (not begun yet)
    for (const char *m : MOVED) {
        std::string from = staging + "/" + m, to = dest + "/" + m;
        if (!exists(from)) continue;
        if (exists(to)) rt_remove_game_file(dest.c_str(), m);
        if (rename(from.c_str(), to.c_str()) != 0) {
            fprintf(stderr, "[import] promoting %s: %s\n", m, strerror(errno));
            return false;
        }
    }
    fsync_path(dest, true);
    remove_staging_tree(staging);
    fprintf(stderr, "[import] the imported game files are in %s\n", dest.c_str());
    return true;
}

void package_import_recover(void) {
    for (int v : VERSIONS) {
        char r[1024];
        if (!rt_version_root(v, r, sizeof r)) continue;
        std::string staging = std::string(r) + "/" + STAGING;
        if (!exists(staging)) continue;
        if (exists(staging + "/" + COMPLETE)) {
            fprintf(stderr, "[import] finishing an import the app was stopped in (v%d)\n", v);
            promote(r);
        } else {
            fprintf(stderr, "[import] removing an unfinished import (v%d)\n", v);
            remove_staging_tree(staging);
        }
    }
}

// romfs.bin's size (runtime/romfs_build.c's layout): 0x200 + each file 16-aligned, then the hash tables (a bucket
// count a little above the entry count: an upper bound) and the directory and file entries
static uint64_t image_size_bound(const nx::RomfsListing &l) {
    auto align4 = [](uint64_t x) { return (x + 3) & ~3ull; };
    uint64_t data = 0, dtab = 0x18, ftab = 0;  // (the root's entry: no name)
    for (auto &f : l.files) {
        size_t s = f.path.find_last_of('/');
        ftab += 0x20 + align4(f.path.size() - (s == std::string::npos ? 0 : s + 1));
        data = ((data + 15) & ~15ull) + f.size;
    }
    for (auto &d : l.dirs) {
        size_t s = d.find_last_of('/');
        dtab += 0x18 + align4(d.size() - (s == std::string::npos ? 0 : s + 1));
    }
    return 0x200 + data + 16 + 4 * (l.dirs.size() + 1 + 64) + dtab + 4 * (l.files.size() + 64) + ftab;
}

static std::string gb(uint64_t b) {
    char s[32];
    snprintf(s, sizeof s, "%.1f GB", double(b) / 1e9);
    return s;
}

static bool write_exefs(const nx::Importer &imp, const std::string &dir, uint64_t &done, uint64_t total,
                        const PackageImportHooks &h, const std::atomic<bool> *cancel, std::string *why) {
    if (mkdir(dir.c_str(), 0755) != 0) return *why = "can't create " + dir + ": " + strerror(errno), false;
    std::vector<uint8_t> buf(1 << 20);
    for (auto &e : imp.exefs()) {
        std::string p = dir + "/" + e.name;
        FILE *f = fopen(p.c_str(), "wb");
        if (!f) return *why = "can't write " + p + ": " + strerror(errno), false;
        uint64_t size = e.data->size();
        for (uint64_t off = 0; off < size;) {
            if (cancel && cancel->load()) {
                fclose(f);
                throw nx::Cancelled();
            }
            uint64_t n = std::min<uint64_t>(size - off, buf.size());
            e.data->read(buf.data(), n, off);
            if (fwrite(buf.data(), 1, size_t(n), f) != n) {
                fclose(f);
                return *why = "write failed (disk full?)", false;
            }
            off += n, done += n;
            if (h.progress) h.progress(done, total);
        }
        if (fflush(f) != 0 || fsync(fileno(f)) != 0 || fclose(f) != 0) return *why = "write failed (disk full?)", false;
    }
    return true;
}

PackageImportOutcome package_import_run(const PackageImportInputs &in, const PackageImportHooks &h,
                                        const std::atomic<bool> *cancel) {
    PackageImportOutcome out;
    auto say = [&](const nx::Error &e) { return h.portuguese ? e.pt() : e.en(); };
    // (descriptors: each owned by its reader once made; whatever wasn't handed over is closed here)
    int fds[4] = {in.base_fd, in.update_fd, in.prod_keys_fd, in.title_keys_fd};
    struct Guard {
        int *fds;
        ~Guard() {
            for (int i = 0; i < 4; i++)
                if (fds[i] >= 0) close(fds[i]);
        }
    } guard{fds};
    auto take = [&](int i) { int fd = fds[i]; fds[i] = -1; return fd; };
    if (h.step) h.step("Opening the game packages...");
    for (int slot = 0; slot < 2; slot++)
        if (fds[slot] >= 0 && !nx::FdReader::seekable(fds[slot], nullptr)) return out.result = PKG_NOT_SEEKABLE, out.slot = slot, out;
    std::unique_ptr<nx::Importer> imp;
    try {
        nx::ImportInput ii;
        ii.base = std::make_shared<nx::FdReader>(take(0), true, in.base_xci ? "base.xci" : "base.nsp");
        ii.base_kind = in.base_xci ? nx::PackageKind::XCI : nx::PackageKind::NSP;
        if (fds[1] >= 0) ii.update = std::make_shared<nx::FdReader>(take(1), true, "update.nsp");
        ii.prod_keys = nx::MemReader::from_fd(take(2), 1u << 20, "prod.keys", true);
        if (fds[3] >= 0) ii.title_keys = nx::MemReader::from_fd(take(3), 16u << 20, "title.keys", true);
        imp.reset(new nx::Importer(ii));  // (the keys' text is wiped when ii goes, below)
        for (auto &l : imp->describe()) fprintf(stderr, "[import] %s\n", l.c_str());
    } catch (const nx::Error &e) {
        out.result = PKG_UNUSABLE, out.detail = say(e);
        return out;
    }
    if (h.verify_only) {
        if (h.step) h.step("Verifying the packages...");
        try {
            imp->verify_ncas(h.progress, cancel);
        } catch (const nx::Cancelled &) {
            return out.result = PKG_CANCELLED, out;
        } catch (const nx::Error &e) {
            fprintf(stderr, "[import] verify: %s\n", e.en().c_str());
            return out.result = PKG_UNUSABLE, out.detail = say(e), out;
        }
        fprintf(stderr, "[import] verify: every NCA matches the SHA-256 its package's metadata records\n");
        return out.result = PKG_DONE, out;
    }

    // the game version: the loader's decision, from the main module's build id
    const nx::Entry *main = nx::find_entry(imp->exefs(), "main");
    uint8_t head[0x48] = {};
    if (!main || main->data->size() < sizeof head) return out.result = PKG_UNUSABLE, out.detail = "exefs/main", out;
    main->data->read(head, sizeof head, 0);
    out.version = memcmp(head, "NSO0", 4) ? 0 : game_version_of_build(head + 0x40);
    char root[1024];
    if (!out.version || !rt_version_root(out.version, root, sizeof root)) return out.result = PKG_VERSION, out;
    mkdir(root, 0755);
    std::string staging = std::string(root) + "/" + STAGING;
    out.dest = std::string(root) + "/extracted";

    // the files against that version's (romfs_check_set, as a picked folder's), before anything is written
    const auto &files = imp->romfs().files;
    std::vector<const char *> paths;
    std::vector<RomfsEntry> entries;
    uint64_t keep_bytes = 0, exefs_bytes = 0;
    for (auto &f : files) {
        paths.push_back(f.path.c_str());
        entries.push_back({f.path.c_str(), f.size});
        if (h.keep_loose && h.keep_loose(f.path.c_str())) keep_bytes += f.size;
    }
    for (auto &e : imp->exefs()) exefs_bytes += e.data->size();
    char why[256] = "";
    std::string vt = imp->version_txt();
    auto check = h.check_set ? h.check_set : romfs_check_set;
    if (check(out.version, paths.data(), int(paths.size()), vt.c_str(), why, sizeof why) != ROMFS_SET_OK)
        return out.result = PKG_ROMFS_SET, out.detail = why, out;

    // the space it needs: romfs.bin, the loose movies, exefs/, and a margin; an earlier setup stays until the end
    remove_staging_tree(staging);  // (an earlier attempt's)
    uint64_t need = image_size_bound(imp->romfs()) + keep_bytes + exefs_bytes;
    need += std::max<uint64_t>(512ull << 20, need / 50);
    struct statvfs fs;
    if (statvfs(root, &fs) == 0) {
        uint64_t free_b = uint64_t(fs.f_bavail) * fs.f_frsize;
        fprintf(stderr, "[import] needs %s, %s free\n", gb(need).c_str(), gb(free_b).c_str());
        if (free_b < need) {
            out.result = PKG_SPACE;
            out.detail = h.portuguese ? "precisa de " + gb(need) + " livres; há " + gb(free_b)
                                      : "needs " + gb(need) + " free; " + gb(free_b) + " free";
            return out;
        }
    }

    try {
        if (mkdir(staging.c_str(), 0755) != 0) return out.result = PKG_WRITE, out.detail = staging + ": " + strerror(errno), out;
        uint64_t done = 0, total = imp->total_bytes();
        std::string err;
        if (h.step) h.step("Copying the game files...");
        if (!write_exefs(*imp, staging + "/exefs", done, total, h, cancel, &err)) {
            remove_staging_tree(staging);
            return out.result = PKG_WRITE, out.detail = err, out;
        }
        if (h.step) h.step("Building the game data image...");
        uint64_t base = done;
        auto progress_hook = h.progress;
        static thread_local std::function<void(uint64_t, uint64_t)> *g_progress;  // (the builder takes a plain function)
        static thread_local uint64_t g_base, g_total;
        g_progress = &progress_hook, g_base = base, g_total = total;
        auto progress = [](uint64_t d, uint64_t) {
            if (*g_progress) (*g_progress)(g_base + d, g_total);
        };
        std::string loose = staging + "/romfs";
        mkdir(loose.c_str(), 0755);
        nx::RomfsFeed feed(*imp, cancel);
        bool ok = romfs_build_entries(entries.data(), int(entries.size()), &nx::RomfsFeed::open_fd, &feed,
                                      (staging + "/romfs.bin").c_str(), loose.c_str(), h.keep_loose, progress, why, sizeof why);
        const nx::Error *fe = feed.finish();
        if (!ok || fe) {
            remove_staging_tree(staging);
            if (fe && dynamic_cast<const nx::Cancelled *>(fe)) return out.result = PKG_CANCELLED, out;
            out.result = fe ? PKG_UNUSABLE : PKG_WRITE;
            out.detail = fe ? say(*fe) : why;
            return out;
        }
        // checked before it replaces anything: the image whole, the staged main the same version
        uint8_t again[0x48] = {};
        FILE *m = fopen((staging + "/exefs/main").c_str(), "rb");
        bool same = m && fread(again, 1, sizeof again, m) == sizeof again && game_version_of_build(again + 0x40) == out.version;
        if (m) fclose(m);
        if (!romfs_image_ok((staging + "/romfs.bin").c_str(), why, sizeof why) || !same) {
            remove_staging_tree(staging);
            return out.result = PKG_WRITE, out.detail = same ? why : "exefs/main", out;
        }
        fsync_path(staging + "/romfs.bin", false);
        std::string c = staging + "/" + COMPLETE;
        FILE *cf = fopen(c.c_str(), "wb");
        bool marked = cf && fprintf(cf, "%d\n", out.version) > 0 && fflush(cf) == 0 && fsync(fileno(cf)) == 0;
        if (cf) marked &= fclose(cf) == 0;
        if (!marked || !fsync_path(staging, true)) {
            remove_staging_tree(staging);
            return out.result = PKG_WRITE, out.detail = "write failed (disk full?)", out;
        }
        if (!promote(root)) return out.result = PKG_WRITE, out.detail = "moving the files into place failed", out;
    } catch (const nx::Cancelled &) {
        remove_staging_tree(staging);
        return out.result = PKG_CANCELLED, out;
    } catch (const nx::Error &e) {
        remove_staging_tree(staging);
        return out.result = PKG_UNUSABLE, out.detail = say(e), out;
    }
    out.result = PKG_DONE;
    return out;
}
