// The player's own NSP/XCI (+ update NSP) and keys, set up as a game version's files (Game Files > Import from
// NSP/XCI; docs/IMPORT-NSP-XCI.md): tools/nximport reads the packages, runtime/romfs_build.c builds romfs.bin straight
// from them (nx::RomfsFeed: no romfs/ folder, one pass of writes), and the result replaces an earlier setup only once
// it's whole:
//   <version root>/extracted.importing/   exefs/, romfs.bin, romfs/ (the loose movies), then COMPLETE
//   -> COMPLETE written (fsync) -> the earlier setup's game files removed, these renamed into <version root>/extracted/
// A cancel, a failure or the app killed before COMPLETE leaves the earlier setup as it was (the staging folder is
// removed at the next start: package_import_recover); killed during the renames, the next start finishes them.
#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

struct PackageImportInputs {
    int base_fd = -1;  // the base .nsp / .xci (owned from here on: closed when done)
    bool base_xci = false;
    int update_fd = -1;      // the update .nsp, or -1
    int prod_keys_fd = -1;   // read whole, then closed
    int title_keys_fd = -1;  // or -1
};

enum PackageImportResult {
    PKG_DONE = 0,
    PKG_CANCELLED,
    PKG_NOT_SEEKABLE,  // a package's descriptor can't be read at any offset (a provider that streams)
    PKG_UNUSABLE,      // keys, packages: nximport's message in detail
    PKG_VERSION,       // not a version this app runs (the loader's build id list)
    PKG_ROMFS_SET,     // romfs_check_set refused the files
    PKG_SPACE,         // not enough free space (detail: how much is needed and free)
    PKG_WRITE,         // writing or promoting failed
};

struct PackageImportOutcome {
    PackageImportResult result = PKG_WRITE;
    std::string detail;  // the cause, in the menu's language (nximport's own messages are English and Portuguese)
    int version = 0;     // 100 / 160 / 182 / 190
    std::string dest;    // <version root>/extracted
    int slot = -1;       // PKG_NOT_SEEKABLE: which package (0 base, 1 update)
};

struct PackageImportHooks {
    std::function<void(const char *step_en)> step;              // a step begins (the launcher's job line, tr()'d)
    std::function<void(uint64_t done, uint64_t total)> progress;  // bytes of the whole import
    bool (*keep_loose)(const char *rel) = nullptr;              // the romfs files kept loose beside romfs.bin
    bool portuguese = false;                                    // the detail in Portuguese
    // Verify packages: nothing written - each NCA's SHA-256 against the one its package's own metadata (CNMT) records,
    // a check independent of the import's hash trees: PKG_DONE when all match, PKG_UNUSABLE naming the one that doesn't
    bool verify_only = false;
    // the files against the version's (default: runtime/romfs_build.c's romfs_check_set; the host tests' stand-in)
    int (*check_set)(int version, const char *const *paths, int n, const char *version_txt, char *why, size_t why_size) = nullptr;
};

PackageImportOutcome package_import_run(const PackageImportInputs &in, const PackageImportHooks &hooks,
                                        const std::atomic<bool> *cancel);

// At start: each version root's staging folder - removed when incomplete, its promotion finished when COMPLETE
void package_import_recover(void);
