# The extraction's third-party code (docs/FRAMEGEN.md): pe-parse (MIT) and DXVK's dxbc (zlib), from
# third_party/lsfg-vk-android's submodules, as one static library; and the commits for the MANIFEST (LSFG_DEFS).
# Needs R (the repository root). Used by the app (CMakeLists.txt) and the host tests (tools/lsfg_extract_tests).
set(LSFG ${R}/third_party/lsfg-vk-android)
if(NOT EXISTS ${LSFG}/thirdparty/dxbc/src/dxbc/dxbc_module.cpp OR NOT EXISTS ${LSFG}/thirdparty/pe-parse/pe-parser-library/src/parse.cpp)
    message(FATAL_ERROR "third_party/lsfg-vk-android is missing: git submodule update --init third_party/lsfg-vk-android && "
                        "git -C third_party/lsfg-vk-android submodule update --init thirdparty/dxbc thirdparty/pe-parse thirdparty/volk")
endif()
set(PEPARSE ${LSFG}/thirdparty/pe-parse/pe-parser-library)
file(GLOB LSFG_DXBC_SRC ${LSFG}/thirdparty/dxbc/src/dxbc/*.cpp ${LSFG}/thirdparty/dxbc/src/spirv/*.cpp ${LSFG}/thirdparty/dxbc/src/util/*.cpp)
add_library(lsfg_deps STATIC ${PEPARSE}/src/buffer.cpp ${PEPARSE}/src/parse.cpp ${PEPARSE}/src/unicode_codecvt.cpp ${LSFG_DXBC_SRC})
set_target_properties(lsfg_deps PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
target_include_directories(lsfg_deps SYSTEM PUBLIC ${PEPARSE}/include ${LSFG}/thirdparty/dxbc/include/dxbc
                           ${LSFG}/thirdparty/dxbc/include/spirv ${LSFG}/thirdparty/dxbc/include/util ${LSFG}/thirdparty/dxbc/include/dxvk)
target_compile_options(lsfg_deps PRIVATE -w)  # (their warnings aren't ours to fix)
# (dxbc includes vulkan/vulkan.h: android/deps/build_deps.sh's Vulkan headers; the app has them on its own path too)
target_include_directories(lsfg_deps SYSTEM PUBLIC ${R}/android/deps/out/include)

find_package(Git QUIET)
set(LSFG_VK_COMMIT unknown)
set(LSFG_DXBC_COMMIT unknown)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} -C ${LSFG} rev-parse --short=12 HEAD OUTPUT_VARIABLE c1 OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    execute_process(COMMAND ${GIT_EXECUTABLE} -C ${LSFG}/thirdparty/dxbc rev-parse --short=12 HEAD OUTPUT_VARIABLE c2
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(c1)
        set(LSFG_VK_COMMIT ${c1})
    endif()
    if(c2)
        set(LSFG_DXBC_COMMIT ${c2})
    endif()
endif()
set(LSFG_DEFS LSFG_VK_COMMIT="${LSFG_VK_COMMIT}" LSFG_DXBC_COMMIT="${LSFG_DXBC_COMMIT}")
