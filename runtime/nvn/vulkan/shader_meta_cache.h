// Binary Maxwell stage translation metadata cache (.smeta) beside the SPIR-V modules in cache/spirv (from BOTWRecomp
// b4e80e7, with TOTK's ShaderInfo). A warm run takes a program's bindings / flags / texture list from the file and its
// SPIR-V from <spv_hash>.spv, so it skips maxwell_translate and shaderc. Included from runtime/nvn/vulkan/exec.c.
//
// What exec.c reads from a translated program (ShaderInfo of both stages, plus per-Prog values derived from the GLSL
// text): everything in ShaderInfo except `msl` and `error`, Prog.vs_uses_vid (vertex GLSL has gl_VertexIndex) and
// Prog.fs_gather (first textureGather slot of the fragment GLSL). All of it is stored below. The key covers the
// shader bytes, the ShaderKey, every translator switch (MwTransCfg), the build and a format / logic version.
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../maxwell/shader.h"

#define MW_META_MAGIC   "TOTKSMT1"
#define MW_META_VERSION 1u  // the file layout
// Bump when exec.c derives the stored per-program values differently (vs_uses_vid, fs_gather), or the translator
// reads something new that the key doesn't cover; the build id (translate.c, shader.h, this file, shaders.c) covers the rest.
#define MW_META_LOGIC   1u

// A new ShaderKey / ShaderInfo / ShaderTexture field must be hashed (key) or stored (file) below first.
_Static_assert(sizeof(bool) == 1 && sizeof(ShaderKey) == 52, "ShaderKey changed: update mw_stage_key");
_Static_assert(sizeof(ShaderTexture) == 20, "ShaderTexture changed: update MwMetaTex");
_Static_assert(sizeof(ShaderInfo) == 1672, "ShaderInfo changed: update MwMetaHdr / mw_meta_store / mw_meta_load");

// The translator's global switches for one translation (maxwell_* in shader.h), the heap placement and the Vulkan API:
// all of them change the GLSL / the metadata. Fixed-width fields only: hashed as raw bytes.
typedef struct {
    uint32_t flags;           // MW_TF_*
    uint32_t env_flags;       // MW_TE_*: the translator's own LARECOMP_* switches
    uint32_t subgroup_stages; // maxwell_subgroup_stages
    uint32_t miss_stages;     // maxwell_gmem_miss_stages
    uint32_t txd_order;       // LARECOMP_TXD_ORDER
    uint32_t vk_api;          // g_vk.api (the SPIR-V target)
    uint64_t gmem_base, gmem_size;
} MwTransCfg;
_Static_assert(sizeof(MwTransCfg) == 40, "MwTransCfg must stay padding free");

enum {
    MW_TF_NO_INT64        = 1u << 0,
    MW_TF_GMEM_MAP_ALL    = 1u << 1,
    MW_TF_GMEM_BDA        = 1u << 2,
    MW_TF_GMEM_MISS_LOG   = 1u << 3,
    MW_TF_BRX_WINDOW      = 1u << 4,
    MW_TF_STATIC_STACKS   = 1u << 5,
    MW_TF_STRUCTURED      = 1u << 6,
    MW_TF_STRUCT_SKIPS    = 1u << 7,
    MW_TF_GFIND_CACHE     = 1u << 8,
    MW_TF_NO_SWITCH       = 1u << 9,
    MW_TF_NO_LOCAL_STRUCT = 1u << 10,
    MW_TF_NATIVE_HALF     = 1u << 11,
    MW_TF_FAST_MINMAX     = 1u << 12,
    MW_TF_CB_WINDOW       = 1u << 13,
    MW_TF_VK_OPT          = 1u << 14,  // (shaders.c: another SPIR-V hash)
    MW_TF_GMEM_CHUNKS     = 1u << 15,  // (the heap in blocks: maxwell_gmem_chunk_shift in bits 16-20)
};
enum {
    MW_TE_NO_VS_SNAP        = 1u << 0,
    MW_TE_FS_SNAP           = 1u << 1,
    MW_TE_NO_GATHER_SNAP    = 1u << 2,
    MW_TE_NO_GATHER_DREF    = 1u << 3,
    MW_TE_NO_TXD            = 1u << 4,
    MW_TE_NO_BANK2          = 1u << 5,
    MW_TE_NO_DISCARD        = 1u << 6,
};

enum {
    MW_BF_WRITES_DEPTH      = 1u << 0,
    MW_BF_USES_DISCARD      = 1u << 1,
    MW_BF_WRITES_POINT_SIZE = 1u << 2,
    MW_BF_WRITES_LAYER      = 1u << 3,
    MW_BF_USES_GMEM         = 1u << 4,
    MW_BF_USES_GMEM_MAP     = 1u << 5,
    MW_BF_USES_GMEM_STORE   = 1u << 6,
    MW_BF_GMEM_BDA          = 1u << 7,
    MW_BF_USES_TINT         = 1u << 8,
    MW_BF_USES_RSCALE       = 1u << 9,
    MW_BF_VS_USES_VID       = 1u << 10,
};

typedef struct {
    char magic[8];
    uint32_t version;
    uint32_t stage;
    uint64_t h1;
    uint64_t h2;
    uint64_t spv_hash;
    uint32_t spv_nwords;
    uint32_t cbuf_mask;
    uint32_t cbuf_size[18];
    uint32_t cbuf_dynamic;
    uint32_t cbwin_mask;
    uint32_t ntex;
    uint32_t attr_mask;
    uint32_t out_mask;
    uint32_t bool_flags;
    uint32_t fs_gather;
    uint32_t local_mem;
    uint32_t num_insts;
    uint32_t reserved;
    uint64_t payload_hash;  // over everything before it, then the textures
} MwMetaHdr;
_Static_assert(sizeof(MwMetaHdr) == 168, "MwMetaHdr must stay 168 bytes without padding");

typedef struct {
    uint32_t handle_offset;
    uint32_t bindless_cbuf;
    uint32_t bindless_offset;
    uint8_t type;
    uint8_t flags;  // 1 shadow, 2 is_array, 4 is_image, 8 bindless
    uint8_t pad[2];
} MwMetaTex;
_Static_assert(sizeof(MwMetaTex) == 16, "MwMetaTex must stay 16 bytes without padding");

static inline int64_t mw_sbits(uint64_t w, int lo, int n) {
    uint64_t mask = (n == 64) ? ~0ull : ((1ull << n) - 1ull);
    int64_t v = (int64_t)((w >> lo) & mask);
    return (v & (1ll << (n - 1))) ? v - (1ll << n) : v;
}

static inline void mw_mix_bytes(uint64_t *h1, uint64_t *h2, const void *p, size_t len) {
    const uint8_t *b = (const uint8_t *)p;
    uint64_t a = *h1, c = *h2;
    for (; len >= 8; len -= 8, b += 8) {
        uint64_t w;
        memcpy(&w, b, 8);
        a = (a ^ w) * 0x9E3779B97F4A7C15ull;
        a ^= a >> 29;
        c = (c + w) * 0xBF58476D1CE4E5B9ull;
        c ^= c >> 31;
    }
    for (; len; len--, b++) {
        a = (a ^ *b) * 0x100000001B3ull;
        c = (c ^ ((uint64_t)(*b) * 0x9E3779B9ull)) * 0x94D049BB133111EBull;
    }
    *h1 = a;
    *h2 = c;
}

static inline void mw_mix_u32(uint64_t *h1, uint64_t *h2, uint32_t v) { mw_mix_bytes(h1, h2, &v, sizeof v); }
static inline void mw_mix_u64(uint64_t *h1, uint64_t *h2, uint64_t v) { mw_mix_bytes(h1, h2, &v, sizeof v); }

// The key of one stage's translation: blob = the guest shader (0x30-byte NVN header, 0x50-byte SPH, code), cmax = its
// code limit (the translator's max_size), const_bytes = the readable constants right after it (BRX tables).
static inline bool mw_stage_key(const uint8_t *blob, uint64_t cmax, size_t const_bytes, int stage,
                                const ShaderKey *key, const MwTransCfg *cfg, const char *build_id,
                                uint64_t *out_h1, uint64_t *out_h2) {
    if (!blob || !key || !cfg || !out_h1 || !out_h2 || cmax <= 0x80 || (stage != STAGE_VERTEX && stage != STAGE_FRAGMENT))
        return false;

    const uint8_t *sph = blob + 0x30;
    const uint8_t *code = blob + 0x80;
    uint64_t nw = (cmax - 0x80) / 8;
    size_t n = nw > 65536 ? 65536 : (size_t)nw;

    // (the translator's own scan for the end of the code: translate.c maxwell_translate_bounded)
    size_t end = 0;
    bool has_brx = false;
    for (size_t i = 0; i < n; i++) {
        uint64_t w;
        memcpy(&w, code + i * 8, sizeof w);
        if (i % 4 == 0) continue;
        if ((w & 0xfff0000000000000ull) == 0xe240000000000000ull && mw_sbits(w, 20, 24) == -8 && end) break;
        if (w == 0 && i > 4) break;
        if ((w & 0xfff0000000000000ull) == 0xe250000000000000ull) has_brx = true;
        end = i + 1;
    }

    uint64_t h1 = 0xcbf29ce484222325ull ^ 0x544f544b534d0001ull;
    uint64_t h2 = 0x9e3779b97f4a7c15ull ^ 0x517cc1b727220a95ull;
    mw_mix_u32(&h1, &h2, MW_META_VERSION);
    mw_mix_u32(&h1, &h2, MW_META_LOGIC);
    const char *bid = (build_id && *build_id) ? build_id : "none";
    size_t bid_len = strlen(bid);
    mw_mix_u64(&h1, &h2, (uint64_t)bid_len);
    mw_mix_bytes(&h1, &h2, bid, bid_len);
    mw_mix_u32(&h1, &h2, (uint32_t)stage);
    mw_mix_bytes(&h1, &h2, cfg, sizeof *cfg);

    // The whole ShaderKey, field by field (its padding is not initialised), bools normalised.
    mw_mix_bytes(&h1, &h2, key->attr_kind, sizeof key->attr_kind);
    mw_mix_bytes(&h1, &h2, key->rt_kind, sizeof key->rt_kind);
    uint8_t kf[8] = {(uint8_t)!!key->depth_minus_one, (uint8_t)!!key->flip_y, (uint8_t)!!key->flip_x,
                     (uint8_t)!!key->no_ds_write, (uint8_t)!!key->gmem_linear_lookup, (uint8_t)!!key->early_z,
                     (uint8_t)!!key->glsl, key->glsl};
    mw_mix_bytes(&h1, &h2, kf, sizeof kf);
    mw_mix_u32(&h1, &h2, key->vs_out_mask);

    mw_mix_bytes(&h1, &h2, sph, 0x50);
    mw_mix_u64(&h1, &h2, (uint64_t)end);
    if (end) mw_mix_bytes(&h1, &h2, code, end * 8);

    // A jump table (BRX) reads the constants after the code (translate.c brx_table).
    if (has_brx && (cfg->flags & MW_TF_BRX_WINDOW) && key->glsl) {
        uint8_t in_range = (cmax < 0x10000 && const_bytes >= 4) ? 1 : 0;
        mw_mix_bytes(&h1, &h2, &in_range, 1);
        if (in_range) {
            size_t words = const_bytes / 4;
            if (words > 1024) words = 1024;
            mw_mix_u32(&h1, &h2, (uint32_t)words);
            mw_mix_bytes(&h1, &h2, blob + cmax, words * 4);
        }
    }

    h1 ^= h1 >> 32;
    h2 ^= h2 >> 32;
    if (!h1) h1 = 1;
    if (!h2) h2 = 1;
    *out_h1 = h1;
    *out_h2 = h2;
    return true;
}

static inline bool mw_meta_exists(const char *dir, uint64_t h1) {
    if (!dir || !*dir || !h1) return false;
    char path[256];
    int pn = snprintf(path, sizeof path, "%s/%016llx.smeta", dir, (unsigned long long)h1);
    if (pn < 0 || (size_t)pn >= sizeof path) return false;
    return access(path, R_OK) == 0;
}

static inline uint64_t mw_meta_payload_hash(const MwMetaHdr *hdr, const MwMetaTex *tex, uint32_t ntex) {
    uint64_t a = 0xcbf29ce484222325ull;
    uint64_t b = 0x243f6a8885a308d3ull;
    mw_mix_bytes(&a, &b, hdr, offsetof(MwMetaHdr, payload_hash));
    if (ntex) mw_mix_bytes(&a, &b, tex, (size_t)ntex * sizeof *tex);
    a ^= b ^ (a >> 32);
    return a ? a : 1;
}

static inline bool mw_meta_store(const char *dir, uint64_t h1, uint64_t h2, int stage, const ShaderInfo *si,
                                 uint64_t spv_hash, size_t spv_nwords, bool vs_uses_vid, uint8_t fs_gather) {
    if (!dir || !*dir || !h1 || !h2 || !si || !spv_hash || !spv_nwords || spv_nwords > 0xFFFFFFFFu || si->ntex < 0 ||
        si->ntex > 64)
        return false;
    if (stage != STAGE_VERTEX && stage != STAGE_FRAGMENT) return false;

    MwMetaHdr hdr;
    memset(&hdr, 0, sizeof hdr);
    memcpy(hdr.magic, MW_META_MAGIC, 8);
    hdr.version = MW_META_VERSION;
    hdr.stage = (uint32_t)stage;
    hdr.h1 = h1;
    hdr.h2 = h2;
    hdr.spv_hash = spv_hash;
    hdr.spv_nwords = (uint32_t)spv_nwords;
    hdr.cbuf_mask = si->cbuf_mask;
    memcpy(hdr.cbuf_size, si->cbuf_size, sizeof hdr.cbuf_size);
    hdr.cbuf_dynamic = si->cbuf_dynamic;
    hdr.cbwin_mask = si->cbwin_mask;
    hdr.ntex = (uint32_t)si->ntex;
    hdr.attr_mask = si->attr_mask;
    hdr.out_mask = si->out_mask;
    hdr.bool_flags = (si->writes_depth ? MW_BF_WRITES_DEPTH : 0u) |
                     (si->uses_discard ? MW_BF_USES_DISCARD : 0u) |
                     (si->writes_point_size ? MW_BF_WRITES_POINT_SIZE : 0u) |
                     (si->writes_layer ? MW_BF_WRITES_LAYER : 0u) |
                     (si->uses_gmem ? MW_BF_USES_GMEM : 0u) |
                     (si->uses_gmem_map ? MW_BF_USES_GMEM_MAP : 0u) |
                     (si->uses_gmem_store ? MW_BF_USES_GMEM_STORE : 0u) |
                     (si->gmem_bda ? MW_BF_GMEM_BDA : 0u) |
                     (si->uses_tint ? MW_BF_USES_TINT : 0u) |
                     (si->uses_rscale ? MW_BF_USES_RSCALE : 0u) |
                     (vs_uses_vid ? MW_BF_VS_USES_VID : 0u);
    hdr.fs_gather = fs_gather;
    hdr.local_mem = si->local_mem;
    hdr.num_insts = si->num_insts;

    MwMetaTex tex[64];
    memset(tex, 0, sizeof tex);
    for (uint32_t i = 0; i < hdr.ntex; i++) {
        const ShaderTexture *st = &si->tex[i];
        tex[i].handle_offset = st->handle_offset;
        tex[i].bindless_cbuf = st->bindless_cbuf;
        tex[i].bindless_offset = st->bindless_offset;
        tex[i].type = st->type;
        tex[i].flags = (uint8_t)((st->shadow ? 1u : 0u) | (st->is_array ? 2u : 0u) | (st->is_image ? 4u : 0u) |
                                 (st->bindless ? 8u : 0u));
    }
    hdr.payload_hash = mw_meta_payload_hash(&hdr, tex, hdr.ntex);

    static _Atomic uint64_t tmp_seq;
    uint64_t seq = atomic_fetch_add_explicit(&tmp_seq, 1, memory_order_relaxed);
    char path[256], tmp[288];
    int pn = snprintf(path, sizeof path, "%s/%016llx.smeta", dir, (unsigned long long)h1);
    if (pn < 0 || (size_t)pn >= sizeof path) return false;
    int tn = snprintf(tmp, sizeof tmp, "%s.%llx.%llx", path, (unsigned long long)(uintptr_t)&tmp_seq, (unsigned long long)seq);
    if (tn < 0 || (size_t)tn >= sizeof tmp) return false;
    FILE *f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(&hdr, sizeof hdr, 1, f) == 1 &&
              (!hdr.ntex || fwrite(tex, sizeof(MwMetaTex), hdr.ntex, f) == hdr.ntex);
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok || rename(tmp, path) != 0) {  // (another thread may have stored it first)
        remove(tmp);
        return false;
    }
    return true;
}

// The program's ShaderInfo (msl NULL: free it with maxwell_free), or NULL for a missing / short / corrupt / old /
// other-key file - the caller then translates.
static inline ShaderInfo *mw_meta_load(const char *dir, uint64_t want_h1, uint64_t want_h2, int want_stage,
                                       uint64_t *out_spv_hash, size_t *out_spv_nwords, bool *out_vs_uses_vid,
                                       uint8_t *out_fs_gather) {
    if (!dir || !*dir || !want_h1 || !want_h2 || !out_spv_hash || !out_spv_nwords ||
        (want_stage != STAGE_VERTEX && want_stage != STAGE_FRAGMENT))
        return NULL;
    char path[256];
    int pn = snprintf(path, sizeof path, "%s/%016llx.smeta", dir, (unsigned long long)want_h1);
    if (pn < 0 || (size_t)pn >= sizeof path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    enum { MW_BUF_CAP = sizeof(MwMetaHdr) + 64 * sizeof(MwMetaTex) + 1 };
    uint8_t buf[MW_BUF_CAP];
    size_t n = fread(buf, 1, sizeof buf, f);
    bool io_err = ferror(f) != 0;
    fclose(f);
    if (io_err || n < sizeof(MwMetaHdr) || n >= MW_BUF_CAP) return NULL;

    MwMetaHdr hdr;
    memcpy(&hdr, buf, sizeof hdr);
    if (memcmp(hdr.magic, MW_META_MAGIC, 8) != 0 || hdr.version != MW_META_VERSION || hdr.stage != (uint32_t)want_stage ||
        hdr.h1 != want_h1 || hdr.h2 != want_h2 || hdr.spv_hash == 0 || hdr.spv_nwords == 0 || hdr.ntex > 64 ||
        hdr.fs_gather == 0 || hdr.fs_gather > 65)
        return NULL;
    if (n != sizeof(MwMetaHdr) + (size_t)hdr.ntex * sizeof(MwMetaTex)) return NULL;

    MwMetaTex tex[64];
    if (hdr.ntex) memcpy(tex, buf + sizeof(MwMetaHdr), (size_t)hdr.ntex * sizeof(MwMetaTex));
    if (mw_meta_payload_hash(&hdr, tex, hdr.ntex) != hdr.payload_hash) return NULL;

    ShaderInfo *si = (ShaderInfo *)calloc(1, sizeof *si);
    if (!si) return NULL;
    si->stage = (int)hdr.stage;
    si->msl = NULL;
    si->cbuf_mask = hdr.cbuf_mask;
    memcpy(si->cbuf_size, hdr.cbuf_size, sizeof si->cbuf_size);
    si->cbuf_dynamic = hdr.cbuf_dynamic;
    si->cbwin_mask = hdr.cbwin_mask;
    si->ntex = (int)hdr.ntex;
    for (uint32_t i = 0; i < hdr.ntex; i++) {
        si->tex[i].handle_offset = tex[i].handle_offset;
        si->tex[i].bindless_cbuf = tex[i].bindless_cbuf;
        si->tex[i].bindless_offset = tex[i].bindless_offset;
        si->tex[i].type = tex[i].type;
        si->tex[i].shadow = (tex[i].flags & 1u) != 0;
        si->tex[i].is_array = (tex[i].flags & 2u) != 0;
        si->tex[i].is_image = (tex[i].flags & 4u) != 0;
        si->tex[i].bindless = (tex[i].flags & 8u) != 0;
    }
    si->attr_mask = hdr.attr_mask;
    si->out_mask = hdr.out_mask;
    si->writes_depth = (hdr.bool_flags & MW_BF_WRITES_DEPTH) != 0;
    si->uses_discard = (hdr.bool_flags & MW_BF_USES_DISCARD) != 0;
    si->writes_point_size = (hdr.bool_flags & MW_BF_WRITES_POINT_SIZE) != 0;
    si->writes_layer = (hdr.bool_flags & MW_BF_WRITES_LAYER) != 0;
    si->uses_gmem = (hdr.bool_flags & MW_BF_USES_GMEM) != 0;
    si->uses_gmem_map = (hdr.bool_flags & MW_BF_USES_GMEM_MAP) != 0;
    si->uses_gmem_store = (hdr.bool_flags & MW_BF_USES_GMEM_STORE) != 0;
    si->gmem_bda = (hdr.bool_flags & MW_BF_GMEM_BDA) != 0;
    si->uses_tint = (hdr.bool_flags & MW_BF_USES_TINT) != 0;
    si->uses_rscale = (hdr.bool_flags & MW_BF_USES_RSCALE) != 0;
    si->local_mem = hdr.local_mem;
    si->num_insts = hdr.num_insts;

    *out_spv_hash = hdr.spv_hash;
    *out_spv_nwords = hdr.spv_nwords;
    if (out_vs_uses_vid) *out_vs_uses_vid = (hdr.bool_flags & MW_BF_VS_USES_VID) != 0;
    if (out_fs_gather) *out_fs_gather = (uint8_t)hdr.fs_gather;
    return si;
}
