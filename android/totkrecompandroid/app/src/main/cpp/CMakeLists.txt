# libmain.so: the TOTKRecompAndroid runtime for Android, with the
# Vulkan backend and the ImGui UI. The game code is not in it: totkgame.so, generated with gen.py --dyn-base and built
# from the player's own game files, is loaded at run time (docs/las/ANDROID-PORT.md).
cmake_minimum_required(VERSION 3.22)
project(totkrecompandroid C CXX ASM)

set(R ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../..)   # the repository
get_filename_component(R ${R} ABSOLUTE)
set(DEPS ${R}/android/deps/out)                        # android/deps/build_deps.sh
if(NOT EXISTS ${DEPS}/lib/libSDL3.so)
    message(FATAL_ERROR "run android/deps/build_deps.sh first")
endif()
if(NOT TOTK_GEN_DIR)
    message(FATAL_ERROR "TOTK_GEN_DIR (the Gradle build's generated files: smaa_hlsl.inc) is not set")
endif()

include(${R}/android/gamelib_id.cmake)  # (GAMELIB_ID: the same as totkgame.so's, android/gamelib)
message(STATUS "GAMELIB_ID ${GAMELIB_ID}")

add_subdirectory(${R}/third_party/libadrenotools ${CMAKE_BINARY_DIR}/adrenotools)
# Import from NSP/XCI (docs/IMPORT-NSP-XCI.md): tools/nximport, the library only (with mbedTLS: third_party/mbedtls)
set(NXIMPORT_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${R}/tools/nximport ${CMAKE_BINARY_DIR}/nximport)
# Frame generation's shaders from the user's Lossless.dll (docs/FRAMEGEN.md): pe-parse and DXVK's dxbc
include(${CMAKE_CURRENT_SOURCE_DIR}/lsfg_deps.cmake)
# The frame generation itself (docs/FRAMEGEN.md B2): lsfg-vk's framegen (MIT) in its own shared library, with its own
# volk, exporting only lsfg_framegen_c.h's functions - its adopted device nulls volk's vkCmdPipelineBarrier2, which must
# not reach libmain's volk (version script, hidden visibility, -Bsymbolic)
set(LSFG_FG ${LSFG}/framegen)
file(GLOB LSFG_FG_SRC ${LSFG_FG}/src/common/*.cpp ${LSFG_FG}/src/core/*.cpp ${LSFG_FG}/src/pool/*.cpp ${LSFG_FG}/v3.1_src/*.cpp
     ${LSFG_FG}/v3.1_src/shaders/*.cpp ${LSFG_FG}/v3.1p_src/*.cpp ${LSFG_FG}/v3.1p_src/shaders/*.cpp)
add_library(lsfg_framegen SHARED ${LSFG_FG_SRC} ${LSFG}/thirdparty/volk/volk.c ${CMAKE_CURRENT_SOURCE_DIR}/lsfg_framegen_c.cpp)
set_target_properties(lsfg_framegen PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON C_VISIBILITY_PRESET hidden
                      CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
target_include_directories(lsfg_framegen PRIVATE ${LSFG_FG}/include ${LSFG_FG}/public ${LSFG_FG}/v3.1_include ${LSFG_FG}/v3.1p_include
                           ${LSFG}/thirdparty/volk ${DEPS}/include)
target_compile_definitions(lsfg_framegen PRIVATE VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR)
target_compile_options(lsfg_framegen PRIVATE -O2 -w)
target_link_options(lsfg_framegen PRIVATE -Wl,-Bsymbolic -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/lsfg_framegen.map)
target_link_libraries(lsfg_framegen PRIVATE log android dl)

add_library(SDL3 SHARED IMPORTED)
set_target_properties(SDL3 PROPERTIES IMPORTED_LOCATION ${DEPS}/lib/libSDL3.so)
add_library(shaderc_shared SHARED IMPORTED)
set_target_properties(shaderc_shared PROPERTIES IMPORTED_LOCATION ${DEPS}/lib/libshaderc_shared.so)

file(GLOB RT_SRC CONFIGURE_DEPENDS ${R}/runtime/*.c ${R}/runtime/hle/*.c ${R}/runtime/services/*.c ${R}/runtime/nvn/*.c
     ${R}/runtime/nvn/maxwell/*.c)
# FFmpeg (android/deps/build_ffmpeg.sh): the game's movies and the start screen's background; without it, neither.
# android/deps/ffmpeg's when it's built (with the Opus decoder: the voices), else android/deps/out's (LASRecompAndroid's)
set(FFMPEG ${R}/android/deps/ffmpeg)
if(NOT EXISTS ${FFMPEG}/lib/libswresample.so)
    set(FFMPEG ${DEPS})
endif()
set(HAVE_FFMPEG FALSE)
if(EXISTS ${FFMPEG}/lib/libavformat.so)
    set(HAVE_FFMPEG TRUE)
    foreach(l avcodec avformat avutil swscale)
        add_library(${l} SHARED IMPORTED)
        set_target_properties(${l} PROPERTIES IMPORTED_LOCATION ${FFMPEG}/lib/lib${l}.so)
    endforeach()
else()
    list(REMOVE_ITEM RT_SRC ${R}/runtime/hle/movie.c)
endif()
file(GLOB VK_SRC CONFIGURE_DEPENDS ${R}/runtime/nvn/vulkan/*.c ${R}/runtime/nvn/vulkan/*.cpp)
file(GLOB UI_SRC CONFIGURE_DEPENDS ${R}/runtime/ui/*.mm)
set(IMGUI ${R}/third_party/imgui)
set(IMGUI_SRC ${IMGUI}/imgui.cpp ${IMGUI}/imgui_draw.cpp ${IMGUI}/imgui_tables.cpp ${IMGUI}/imgui_widgets.cpp
    ${IMGUI}/backends/imgui_impl_sdl3.cpp ${IMGUI}/backends/imgui_impl_vulkan.cpp)

add_library(main SHARED ${RT_SRC} ${R}/runtime/platform/android.c ${VK_SRC} ${UI_SRC} ${IMGUI_SRC}
            ${DEPS}/volk/volk.c ${R}/android/common/guest_window.c ${R}/runtime/native_gate.S
            ${CMAKE_CURRENT_SOURCE_DIR}/package_import.cpp ${CMAKE_CURRENT_SOURCE_DIR}/lsfg_extract.cpp
            ${CMAKE_CURRENT_SOURCE_DIR}/lsfg_translate.cpp)
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/lsfg_extract.cpp ${CMAKE_CURRENT_SOURCE_DIR}/lsfg_translate.cpp
                            PROPERTIES COMPILE_DEFINITIONS "${LSFG_DEFS}")
# (SPIRV-Tools' validator is inside libshaderc_shared; its header from the shaderc checkout build_deps.sh made)
set(SPIRV_TOOLS_INC ${R}/android/deps/work/shaderc/third_party/spirv-tools/include)
if(EXISTS ${SPIRV_TOOLS_INC}/spirv-tools/libspirv.h)
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/lsfg_translate.cpp APPEND PROPERTY COMPILE_DEFINITIONS LSFG_HAVE_SPIRV_TOOLS)
    set_property(SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/lsfg_translate.cpp APPEND PROPERTY INCLUDE_DIRECTORIES ${SPIRV_TOOLS_INC})
else()
    message(WARNING "SPIRV-Tools' header not found (${SPIRV_TOOLS_INC}): Lossless.dll's shaders get the structural checks only")
endif()
# the UI sources are plain C++ apart from __APPLE__ blocks (compiled as C++ on Android)
set_source_files_properties(${UI_SRC} PROPERTIES LANGUAGE CXX COMPILE_OPTIONS "-xc++")
set_source_files_properties(${R}/runtime/gamelib.c PROPERTIES COMPILE_DEFINITIONS "GAMELIB_ID=\"${GAMELIB_ID}\"")
set_source_files_properties(${R}/runtime/gamebuild.c PROPERTIES COMPILE_DEFINITIONS "GAMELIB_ID=\"${GAMELIB_ID}\"")

target_include_directories(main PRIVATE ${R}/runtime ${R}/runtime/nvn ${IMGUI} ${IMGUI}/backends ${FFMPEG}/include ${DEPS}/include
                           ${DEPS}/volk ${TOTK_GEN_DIR} ${R}/third_party/smaa/Textures ${R}/android/common
                           ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(main PRIVATE
    VK_NO_PROTOTYPES VK_USE_PLATFORM_ANDROID_KHR IMGUI_IMPL_VULKAN_USE_VOLK   # Vulkan from the loaded driver (volk)
    VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1)
if(HAVE_FFMPEG)
    target_link_libraries(main PRIVATE avcodec avformat avutil swscale)
else()
    target_compile_definitions(main PRIVATE LARECOMP_NO_FFMPEG)
endif()
# -O2 -g whatever the Gradle build type (the Android runtime optimization flags): a debug build left the runtime at -O0, about half of the
# game thread's time
target_compile_options(main PRIVATE -O2 -g -fno-strict-aliasing -Wall -Wno-unused-function -Wno-format  # (as on Linux: %llx for uint64_t)
    $<$<COMPILE_LANGUAGE:CXX>:-std=c++17>)
target_link_libraries(main PRIVATE SDL3 shaderc_shared adrenotools nximport lsfg_deps lsfg_framegen android log dl m)
# (runtime/hle.h, ipc.h, main.c: tables in named sections found through __start_ / __stop_ symbols)
target_link_options(main PRIVATE -Wl,-z,nostart-stop-gc)
