#include "nximport.h"

#include "bktr.h"
#include "cnmt.h"
#include "nca.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>

namespace nx {

enum : uint64_t { MAX_PROD_KEYS = 1u << 20, MAX_TITLE_KEYS = 16u << 20, COPY_CHUNK = 4u << 20 };

static std::string tid(uint64_t id) {
    char b[17];
    snprintf(b, sizeof b, "%016llX", (unsigned long long)id);
    return b;
}

static bool ends_with(const std::string &s, const char *suffix) {
    size_t n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

struct Package {
    std::string what;
    std::vector<Entry> files;
    std::vector<Cnmt> metas;
};

struct Picked {
    Cnmt meta;
    const Package *package;
    std::shared_ptr<Nca> program;
    std::string program_name;
};

struct Importer::Impl {
    KeySet keys;
    std::vector<Ticket> tickets;
    std::vector<Package> packages;
    Picked base, update;
    bool has_update = false;
    std::vector<Entry> exefs;
    ReaderPtr romfs_image;
    RomfsListing romfs;

    void load_keys(const ImportInput &in) {
        std::vector<uint8_t> t = read_small_file(*in.prod_keys, MAX_PROD_KEYS);
        try {
            keys.parse_prod_keys(t.data(), t.size());
        } catch (...) {
            wipe(t.data(), t.size());
            throw;
        }
        wipe(t.data(), t.size());
        if (in.title_keys) {
            std::vector<uint8_t> k = read_small_file(*in.title_keys, MAX_TITLE_KEYS);
            try {
                keys.parse_title_keys(k.data(), k.size());
            } catch (...) {
                wipe(k.data(), k.size());
                throw;
            }
            wipe(k.data(), k.size());
        }
        keys.derive();
    }

    Package open(const ReaderPtr &file, PackageKind kind) {
        Package p;
        p.what = file->what();
        p.files = open_package(file, kind);
        for (auto &e : p.files)
            if (ends_with(e.name, ".tik")) tickets.push_back(parse_ticket(*e.data));
        return p;
    }

    // Each package's CNMTs (from its Meta NCAs; tickets of every package known by then)
    void read_metas(Package &p) {
        for (auto &e : p.files) {
            if (!ends_with(e.name, ".cnmt.nca")) continue;
            Nca meta(e.data, keys, tickets);
            if (meta.content_type() != ContentType::Meta) corrupt(e.name + " (not a Meta NCA)");
            for (auto &s : meta.sections()) {
                if (s.fs_type != FS_PFS0) continue;
                for (auto &f : meta.open_pfs0(s))
                    if (ends_with(f.name, ".cnmt")) p.metas.push_back(parse_cnmt(*f.data));
            }
        }
    }

    Picked pick(const Package &p, uint8_t type, uint64_t title) {
        const Cnmt *found = nullptr;
        for (auto &m : p.metas) {
            if (m.type != type || m.title_id != title) continue;
            if (type == META_PATCH && m.application_id != BOTW_TITLE_ID) continue;
            if (found)
                fail(p.what + " holds more than one " + (type == META_PATCH ? "update" : "copy") + " of the game: choose a package with one",
                     p.what + " tem mais de um " + (type == META_PATCH ? "update" : "jogo base") + " deste jogo: escolha um pacote com um só");
            found = &m;
        }
        if (!found) {
            std::string others;
            for (auto &m : p.metas) others += (others.empty() ? "" : ", ") + tid(m.title_id);
            if (others.empty()) others = "none";
            if (type == META_PATCH)
                fail(p.what + " is not an update of Breath of the Wild (" + tid(BOTW_PATCH_ID) + "); it holds: " + others,
                     p.what + " não é um update do Breath of the Wild (" + tid(BOTW_PATCH_ID) + "); ele contém: " + others);
            fail(p.what + " is not Breath of the Wild (" + tid(BOTW_TITLE_ID) + "); it holds: " + others,
                 p.what + " não é o Breath of the Wild (" + tid(BOTW_TITLE_ID) + "); ele contém: " + others);
        }
        const CnmtContent *c = found->find(CNT_PROGRAM);
        if (!c) corrupt(p.what + " (no program in the CNMT)");
        const Entry *e = find_entry(p.files, c->file_name());
        if (!e)
            fail(p.what + ": the game's program " + c->file_name() + " is missing from the package (incomplete dump?)",
                 p.what + ": o programa do jogo " + c->file_name() + " não está no pacote (dump incompleto?)");
        if (e->data->size() != c->size) corrupt(e->name + " (size differs from its CNMT record)");
        auto nca = std::make_shared<Nca>(e->data, keys, tickets);
        // (an update's program may carry the application's id or the patch's: the CNMT already said which content it is)
        if (nca->content_type() != ContentType::Program || (nca->title_id() != BOTW_TITLE_ID && nca->title_id() != found->title_id))
            corrupt(e->name + " (not the game's program)");
        return {*found, &p, nca, e->name};
    }

    void open_exefs(const Nca &program) {
        for (auto &s : program.sections()) {
            if (s.fs_type != FS_PFS0) continue;
            auto files = program.open_pfs0(s);
            if (find_entry(files, "main") && find_entry(files, "main.npdm")) {
                exefs = std::move(files);
                return;
            }
        }
        corrupt(program.what() + " (no exefs)");
    }

    static const Section *romfs_section(const Nca &nca) {
        for (auto &s : nca.sections())
            if (s.fs_type == FS_ROMFS) return &s;
        corrupt(nca.what() + " (no RomFS)");
    }

    void open_romfs() {
        const Nca &nca = *base.program;
        const Section *bs = romfs_section(nca);
        if (has_update) {  // (the base's section raw: the patch's ranges point into it, bktr.h)
            const Nca &p = *update.program;
            romfs_image = open_patched_romfs(p, *romfs_section(p), nca.raw_section(*bs));
        } else {
            romfs_image = nca.open_romfs(*bs);
        }
        romfs = list_romfs(*romfs_image);
    }
};

Importer::Importer(const ImportInput &in) : impl_(new Impl) {
    Impl &m = *impl_;
    m.load_keys(in);
    m.packages.reserve(2);
    m.packages.push_back(m.open(in.base, in.base_kind));
    if (in.update) m.packages.push_back(m.open(in.update, PackageKind::NSP));
    for (auto &p : m.packages) m.read_metas(p);
    m.base = m.pick(m.packages[0], META_APPLICATION, BOTW_TITLE_ID);
    if (in.update) {
        m.has_update = true;
        m.update = m.pick(m.packages[1], META_PATCH, BOTW_PATCH_ID);
    }
    m.open_exefs(m.has_update ? *m.update.program : *m.base.program);
    m.open_romfs();
}

Importer::~Importer() = default;

uint32_t Importer::base_version() const { return impl_->base.meta.version; }
uint32_t Importer::update_version() const { return impl_->has_update ? impl_->update.meta.version : 0; }
bool Importer::has_update() const { return impl_->has_update; }
const std::vector<Entry> &Importer::exefs() const { return impl_->exefs; }
const RomfsListing &Importer::romfs() const { return impl_->romfs; }

void Importer::read_romfs(size_t file, void *dst, uint64_t n, uint64_t off) const {
    const RomfsFile &f = impl_->romfs.files.at(file);
    check_range(off, n, f.size, f.path.c_str());
    impl_->romfs_image->read(dst, n, add(f.offset, off, f.path.c_str()));
}

std::string Importer::version_txt() const {
    auto &files = impl_->romfs.files;
    for (size_t i = 0; i < files.size(); i++)
        if (files[i].path == "System/Version.txt") {
            uint64_t n = files[i].size < 64 ? files[i].size : 64;
            std::string s(size_t(n), '\0');
            read_romfs(i, &s[0], n, 0);
            return s;
        }
    return "";
}

uint64_t Importer::total_bytes() const {
    uint64_t t = impl_->romfs.total_bytes;
    for (auto &e : impl_->exefs) t = add(t, e.data->size(), "exefs");
    return t;
}

static void make_dir(const std::string &p) {
    if (mkdir(p.c_str(), 0755) != 0 && errno != EEXIST) {
        std::string e = strerror(errno);
        fail("Can't create " + p + " (" + e + ")", "Não foi possível criar " + p + " (" + e + ")");
    }
}

// One file: n bytes from read(buf, n, off) into path
template <class Read>
static void write_file(const std::string &path, uint64_t size, Read read, std::vector<uint8_t> &buf, uint64_t &done,
                       uint64_t total, const Progress &progress, const std::atomic<bool> *cancel) {
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        std::string e = strerror(errno);
        fail("Can't write " + path + " (" + e + ")", "Não foi possível gravar " + path + " (" + e + ")");
    }
    try {
        for (uint64_t off = 0; off < size;) {
            if (cancel && cancel->load()) throw Cancelled();
            uint64_t n = size - off < buf.size() ? size - off : buf.size();
            read(buf.data(), n, off);
            if (fwrite(buf.data(), 1, size_t(n), f) != n) {
                std::string e = strerror(errno);
                fail("Write failed: " + path + " (" + e + "; disk full?)", "Falha ao gravar: " + path + " (" + e + "; disco cheio?)");
            }
            off += n, done += n;
            if (progress) progress(done, total);
        }
    } catch (...) {
        fclose(f);
        throw;
    }
    if (fclose(f) != 0) fail("Write failed: " + path + " (disk full?)", "Falha ao gravar: " + path + " (disco cheio?)");
}

void Importer::extract_to(const std::string &dir, const Progress &progress, const std::atomic<bool> *cancel) const {
    make_dir(dir);
    make_dir(dir + "/exefs");
    make_dir(dir + "/romfs");
    std::vector<uint8_t> buf(COPY_CHUNK);
    uint64_t done = 0, total = total_bytes();
    for (auto &e : impl_->exefs) {
        const Reader &r = *e.data;
        write_file(dir + "/exefs/" + e.name, r.size(), [&](void *d, uint64_t n, uint64_t o) { r.read(d, n, o); }, buf, done,
                   total, progress, cancel);
    }
    for (auto &d : impl_->romfs.dirs) make_dir(dir + "/romfs/" + d);
    auto &files = impl_->romfs.files;
    for (size_t i = 0; i < files.size(); i++)
        write_file(dir + "/romfs/" + files[i].path, files[i].size,
                   [&](void *d, uint64_t n, uint64_t o) { read_romfs(i, d, n, o); }, buf, done, total, progress, cancel);
}

void Importer::verify_ncas(const Progress &progress, const std::atomic<bool> *cancel) const {
    std::vector<std::pair<const Picked *, const CnmtContent *>> todo;
    for (const Picked *p : {&impl_->base, impl_->has_update ? &impl_->update : nullptr}) {
        if (!p) continue;
        for (auto &c : p->meta.contents)
            if (find_entry(p->package->files, c.file_name())) todo.push_back({p, &c});
    }
    uint64_t total = 0, done = 0;
    for (auto &t : todo) total += t.second->size;
    std::vector<uint8_t> buf(COPY_CHUNK);
    for (auto &t : todo) {
        const Entry *e = find_entry(t.first->package->files, t.second->file_name());
        Sha256 h;
        for (uint64_t off = 0, size = e->data->size(); off < size;) {
            if (cancel && cancel->load()) throw Cancelled();
            uint64_t n = size - off < buf.size() ? size - off : buf.size();
            e->data->read(buf.data(), n, off);
            h.update(buf.data(), size_t(n));
            off += n, done += n;
            if (progress) progress(done, total);
        }
        uint8_t d[32];
        h.finish(d);
        if (memcmp(d, t.second->hash.data(), 32) != 0 || memcmp(d, t.second->id.data(), 16) != 0)
            fail(e->name + ": its SHA-256 doesn't match the package's metadata (corrupt or modified file)",
                 e->name + ": o SHA-256 não bate com os metadados do pacote (arquivo corrompido ou modificado)");
    }
}

std::string Importer::base_program() const { return impl_->base.program_name; }
std::string Importer::update_program() const { return impl_->has_update ? impl_->update.program_name : ""; }

std::vector<std::string> Importer::describe() const {
    std::vector<std::string> d;
    auto one = [&](const char *what, const Picked &p) {
        d.push_back(std::string(what) + ": " + tid(p.meta.title_id) + " v" + std::to_string(p.meta.version) + " (" +
                    p.package->what + ", program " + p.program->what() + ", key revision " +
                    std::to_string(p.program->key_revision()) + (p.program->has_rights_id() ? ", title key" : "") + ")");
    };
    one("base", impl_->base);
    if (impl_->has_update) one("update", impl_->update);
    d.push_back("exefs: " + std::to_string(impl_->exefs.size()) + " files; romfs: " + std::to_string(impl_->romfs.files.size()) +
                " files, " + std::to_string(impl_->romfs.total_bytes) + " bytes");
    return d;
}

}  // namespace nx
