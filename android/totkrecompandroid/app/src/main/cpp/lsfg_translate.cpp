// DXBC -> SPIR-V for the frame generation's shaders, with DXVK's dxbc (zlib; third_party/lsfg-vk-android/thirdparty/dxbc),
// the bindings then numbered 0, 1, 2 ... in declaration order - the order of the framegen's descriptor layouts (as
// lsfg-vk-android's src/extract/trans.cpp does, MIT). The SPIR-V checked by SPIRV-Tools (in libshaderc_shared).
// And the app's side: lsfg_import_fd / lsfg_state / lsfg_remove / lsfg_startup (lsfg_extract.h).
#include "lsfg_extract.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <atomic>
#include <mutex>

#include <dxbc_module.h>
#include <dxbc_modinfo.h>
#include <dxbc_reader.h>

#ifdef LSFG_HAVE_SPIRV_TOOLS
#include <spirv-tools/libspirv.h>
#endif

namespace lsfg {

bool dxbc_translate(const std::vector<uint8_t> &dxbc, std::vector<uint32_t> &out, std::string &err) {
    try {
        dxvk::DxbcReader reader(reinterpret_cast<const char *>(dxbc.data()), dxbc.size());
        dxvk::DxbcModule module(reader);
        const dxvk::DxbcModuleInfo info{};
        auto code = module.compile(info, "CS");
        // the variables' Binding decorations, in order (only those with one)
        std::vector<uint32_t> at;  // (the dword of each binding number)
        for (auto ins : code) {
            if (ins.opCode() == spv::OpFunction) break;
            if (ins.opCode() == spv::OpDecorate && ins.length() >= 4 && ins.arg(2) == spv::DecorationBinding) at.push_back(ins.offset() + 3);
        }
        uint32_t *w = code.data();
        for (size_t i = 0; i < at.size(); i++)
            if (at[i] < code.dwords()) w[at[i]] = uint32_t(i);
        out.assign(w, w + code.dwords());
        return true;
    } catch (const dxvk::DxvkError &e) {
        err = e.message();
    } catch (const std::exception &e) {
        err = e.what();
    } catch (...) {
        err = "translation failed";
    }
    return false;
}

bool spirv_validate(const std::vector<uint32_t> &spirv, std::string &err) {
#ifdef LSFG_HAVE_SPIRV_TOOLS
    spv_context ctx = spvContextCreate(SPV_ENV_VULKAN_1_3);  // (dxbc writes SPIR-V 1.6: Vulkan 1.3)
    spv_diagnostic diag = nullptr;
    spv_const_binary_t bin = {spirv.data(), spirv.size()};
    spv_result_t r = spvValidate(ctx, &bin, &diag);
    if (r != SPV_SUCCESS) err = diag && diag->error ? diag->error : "invalid SPIR-V";
    spvDiagnosticDestroy(diag);
    spvContextDestroy(ctx);
    return r == SPV_SUCCESS;
#else
    (void)spirv, (void)err;
    return true;  // (the structure is still checked: spirv_bindings)
#endif
}

}  // namespace lsfg

// ---------------------------------------------------------------- the app's side
#ifdef __ANDROID__
#include <SDL3/SDL_system.h>

static std::mutex g_lock;
static std::atomic<int> g_busy{0};
static std::string g_last;  // (the last import's outcome, for the menu)

static std::string root_dir() {
    const char *in = SDL_GetAndroidInternalStoragePath();  // (getFilesDir: private to the app, not backed up as a file set)
    return in ? in : "";
}

extern "C" void lsfg_startup(void) {
    std::string r = root_dir();
    if (!r.empty()) lsfg::recover(r);
}

extern "C" int lsfg_import_fd(int fd, char *detail, size_t n) {
    std::string r = root_dir();
    if (r.empty()) {
        if (fd >= 0) close(fd);
        snprintf(detail, n, "no app folder");
        return lsfg::WRITE;
    }
    g_busy = 1;
    lsfg::Report rep;
    lsfg::Result res = lsfg::extract_fd(fd, r, lsfg::dxbc_translate, lsfg::spirv_validate, rep);
    g_busy = 0;
    fprintf(stderr, "[lsfg] Lossless.dll: result %d, %s%s%s\n", int(res), rep.detail.c_str(), rep.dll_sha256.empty() ? "" : ", sha256 ",
            rep.dll_sha256.c_str());
    if (res == lsfg::OK) {
        std::string caps;
        for (auto &c : rep.capabilities) caps += " " + c;
        fprintf(stderr, "[lsfg] the shaders' capabilities:%s\n", caps.c_str());
    }
    {
        std::lock_guard<std::mutex> l(g_lock);
        g_last = res == lsfg::OK ? "" : rep.detail;
    }
    snprintf(detail, n, "%s", rep.detail.c_str());
    return int(res);
}

extern "C" int lsfg_state(char *line, size_t n) {
    if (g_busy) { snprintf(line, n, "%s", ""); return 2; }
    std::string s;
    if (lsfg::installed(root_dir(), &s)) { snprintf(line, n, "%s", s.c_str()); return 1; }
    std::lock_guard<std::mutex> l(g_lock);
    snprintf(line, n, "%s", g_last.c_str());
    return 0;
}

extern "C" int lsfg_installed_set(char *dir, size_t ndir, char *caps, size_t ncaps) {
    std::string r = root_dir();
    std::vector<std::string> c;
    if (r.empty() || !lsfg::installed(r, nullptr, &c)) return 0;
    snprintf(dir, ndir, "%s/lsfg/spirv", r.c_str());
    std::string s;
    for (auto &x : c) s += (s.empty() ? "" : " ") + x;
    snprintf(caps, ncaps, "%s", s.c_str());
    return 1;
}

extern "C" void lsfg_remove(void) {
    std::string r = root_dir();
    if (!r.empty()) lsfg::remove_all(r);
    std::lock_guard<std::mutex> l(g_lock);
    g_last.clear();
}
#endif
