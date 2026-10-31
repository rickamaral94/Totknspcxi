// nximport CLI: the player's own NSP/XCI (+ update) and keys into Dump/0100F2C0115B6000/{exefs,romfs}, the layout
// android/build_gamelib.sh, android/push_game.sh and tools/totk_dump.py take. Written into <title id>.partial and
// renamed when whole: an interrupted run leaves no folder that looks finished.
#include "nximport.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ftw.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static std::atomic<bool> g_cancel{false};
static bool g_pt;

static void on_signal(int) { g_cancel = true; }

static const char *msg(const nx::Error &e) { return g_pt ? e.pt().c_str() : e.en().c_str(); }

static int usage() {
    fprintf(stderr,
            "usage: nximport --keys prod.keys [--title-keys title.keys] (--nsp base.nsp | --xci game.xci)\n"
            "                [--update update.nsp] -o <out dir> [--verify-nca] [--list]\n"
            "  writes <out dir>/0100F2C0115B6000/exefs and .../romfs (as hactool extracts them)\n"
            "  --verify-nca  also check each NCA's SHA-256 against the package metadata (reads the packages once more)\n"
            "  --list        print what would be extracted, write nothing\n"
            "  --programs    print the Program NCAs used (\"base <file>\", \"update <file>\"), write nothing\n");
    return 2;
}

static int rm_entry(const char *p, const struct stat *, int, struct FTW *) { return remove(p); }
static void rm_tree(const std::string &p) { nftw(p.c_str(), rm_entry, 16, FTW_DEPTH | FTW_PHYS); }

static bool exists(const std::string &p) {
    struct stat st;
    return stat(p.c_str(), &st) == 0;
}

int main(int argc, char **argv) {
    const char *lang = getenv("LANG");
    g_pt = lang && !strncmp(lang, "pt", 2);
    std::string keys, title_keys, base, update, out;
    nx::PackageKind kind = nx::PackageKind::NSP;
    bool verify = false, list = false, programs = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--keys") keys = next();
        else if (a == "--title-keys") title_keys = next();
        else if (a == "--nsp") base = next(), kind = nx::PackageKind::NSP;
        else if (a == "--xci") base = next(), kind = nx::PackageKind::XCI;
        else if (a == "--update") update = next();
        else if (a == "-o") out = next();
        else if (a == "--verify-nca") verify = true;
        else if (a == "--list") list = true;
        else if (a == "--programs") programs = true;
        else return usage();
    }
    if (keys.empty() || base.empty() || (out.empty() && !list && !programs)) return usage();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    std::string dest = out + "/0100F2C0115B6000", partial = dest + ".partial";
    try {
        nx::ImportInput in;
        in.prod_keys = nx::open_file(keys);
        if (!title_keys.empty()) in.title_keys = nx::open_file(title_keys);
        in.base = nx::open_file(base);
        in.base_kind = kind;
        if (!update.empty()) in.update = nx::open_file(update);
        nx::Importer imp(in);
        for (auto &line : imp.describe()) fprintf(stderr, "[nximport] %s\n", line.c_str());
        if (programs) {
            printf("base %s\n", imp.base_program().c_str());
            if (imp.has_update()) printf("update %s\n", imp.update_program().c_str());
            return 0;
        }
        if (list) {
            for (auto &e : imp.exefs()) printf("exefs/%s %llu\n", e.name.c_str(), (unsigned long long)e.data->size());
            for (auto &f : imp.romfs().files) printf("romfs/%s %llu\n", f.path.c_str(), (unsigned long long)f.size);
            return 0;
        }
        int last = -1;
        const bool tty = isatty(2);
        auto progress = [&](uint64_t done, uint64_t total) {
            int pct = total ? int(done * 100 / total) : 100;
            // (a terminal: one line redrawn; a log: a line every 10%)
            if (pct == last || (!tty && pct % 10 && pct != 100)) return;
            fprintf(stderr, tty ? "\r[nximport] %3d%% (%llu / %llu MiB)" : "[nximport] %3d%% (%llu / %llu MiB)\n", last = pct,
                    (unsigned long long)(done >> 20), (unsigned long long)(total >> 20));
        };
        if (verify) {
            fprintf(stderr, "[nximport] checking the NCAs' SHA-256...\n");
            imp.verify_ncas(progress, &g_cancel);
            fprintf(stderr, "\n");
            last = -1;
        }
        if (exists(dest)) {
            fprintf(stderr, g_pt ? "[nximport] %s já existe: apague ou escolha outra pasta\n"
                                 : "[nximport] %s already exists: delete it or choose another folder\n", dest.c_str());
            return 1;
        }
        mkdir(out.c_str(), 0755);
        rm_tree(partial);  // (an earlier interrupted run's)
        imp.extract_to(partial, progress, &g_cancel);
        fprintf(stderr, "\n");
        if (rename(partial.c_str(), dest.c_str()) != 0) {
            fprintf(stderr, "[nximport] rename %s: %s\n", partial.c_str(), strerror(errno));
            return 1;
        }
        fprintf(stderr, g_pt ? "[nximport] pronto: %s\n" : "[nximport] done: %s\n", dest.c_str());
        return 0;
    } catch (const nx::Cancelled &) {
        fprintf(stderr, "\n[nximport] %s\n", g_pt ? "cancelado" : "cancelled");
        rm_tree(partial);
        return 130;
    } catch (const nx::Error &e) {
        fprintf(stderr, "\n[nximport] %s\n", msg(e));
        rm_tree(partial);
        return 1;
    }
}
