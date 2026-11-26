# Writes OUT with MW_TRANSLATOR_ID: a hash of the sources that decide what a translated shader (and its SPIR-V) is, so the
# .smeta cache (runtime/nvn/vulkan/shader_meta_cache.h) from another translator is never used (from BOTWRecomp b4e80e7).
# Run on every build; the file changes only when the value does.
set(mw_parts "")
foreach(f runtime/nvn/maxwell/translate.c runtime/nvn/maxwell/shader.h runtime/nvn/vulkan/shader_meta_cache.h
          runtime/nvn/vulkan/shader_cache.h runtime/nvn/vulkan/shaders.c)
    if(EXISTS "${R}/${f}")
        file(SHA256 "${R}/${f}" fh)
        string(APPEND mw_parts "${f}:${fh}\n")
    else()
        string(APPEND mw_parts "${f}:missing\n")
    endif()
endforeach()
string(SHA256 mw_hash "${mw_parts}")
string(SUBSTRING "${mw_hash}" 0 16 mw_id)
set(text "#define MW_TRANSLATOR_ID \"${mw_id}\"\n")
set(old "")
if(EXISTS ${OUT})
    file(READ ${OUT} old)
endif()
if(NOT old STREQUAL text)
    file(WRITE ${OUT} "${text}")
endif()
