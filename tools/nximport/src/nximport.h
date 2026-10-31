// nximport: the player's own NSP/XCI (+ update NSP) and keys into the extracted game files the rest of the project
// takes (exefs/ + romfs/, as hactool extracts them). The desktop CLI (cli/main.cpp) and the Android app (JNI,
// android/totkrecompandroid/app/src/main/cpp/nximport_jni.cpp) share this.
//
// The game is found through the packages' content metadata: the base's Application CNMT of 0100F2C0115B6000 and,
// with an update, its Patch CNMT of 0100F2C0115B6800; their Program NCAs by content id. Which game version that is
// stays the loader's decision (runtime/loader.c, game_version_of_build, on exefs/main): nothing here keeps a list
// of versions.
#pragma once
#include "container.h"
#include "keys.h"
#include "romfs.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace nx {

constexpr uint64_t GAME_TITLE_ID = 0x0100F2C0115B6000ull;  // The Legend of Zelda: Tears of the Kingdom
constexpr uint64_t GAME_PATCH_ID = 0x0100F2C0115B6800ull;  // its updates (1.4.3: v720896)

struct ImportInput {
    ReaderPtr base;  // .nsp or .xci
    PackageKind base_kind = PackageKind::NSP;
    ReaderPtr update;  // the update .nsp, or null
    ReaderPtr prod_keys;
    ReaderPtr title_keys;  // or null
};

using Progress = std::function<void(uint64_t done, uint64_t total)>;

class Importer {
public:
    explicit Importer(const ImportInput &in);
    ~Importer();

    uint32_t base_version() const;    // the base's CNMT version (0 for 1.0.0)
    uint32_t update_version() const;  // the update's, 0 without one
    bool has_update() const;

    // exefs/: main, main.npdm, rtld, sdk, subsdk* (the update's when there is one: it replaces the base's)
    const std::vector<Entry> &exefs() const;
    // romfs/: the RomFS (the update applied over the base's), every read checked against its hash tree
    const RomfsListing &romfs() const;
    void read_romfs(size_t file, void *dst, uint64_t n, uint64_t off) const;
    // The first bytes of romfs/System/Version.txt ("" without one), as the app's romfs check takes them
    std::string version_txt() const;
    uint64_t total_bytes() const;  // exefs + romfs file data

    // Writes dir/exefs and dir/romfs (dir made, must not hold them yet). progress (may be null) now and then;
    // cancel (may be null) checked between blocks: throws Cancelled.
    void extract_to(const std::string &dir, const Progress &progress, const std::atomic<bool> *cancel) const;

    // SHA-256 of each NCA used against its CNMT record (a whole extra read of the packages; optional). An NCA whose
    // SHA-256 differs gets its sections' own hash trees read whole: all matching, only its header was changed (a dump
    // or conversion tool rewrote it: the content is Nintendo's) - named in the returned list, not a failure. Else
    // HashMismatch, saying which section's data differs.
    std::vector<std::string> verify_ncas(const Progress &progress, const std::atomic<bool> *cancel) const;

    // The Program NCAs used, by their file names in the packages ("" for the update without one): verify.sh hands
    // the same NCAs to hactool
    std::string base_program() const;
    std::string update_program() const;

    // What was opened, for logs (titles, versions, NCA names; never keys)
    std::vector<std::string> describe() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace nx
