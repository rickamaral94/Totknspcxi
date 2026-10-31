# nximport: NSP/XCI + keys -> exefs/ + romfs/ (README: docs/IMPORT-NSP-XCI.md). A static library the Android app links
# (add_subdirectory from android/totkrecompandroid/app/src/main/cpp/CMakeLists.txt), the desktop CLI and the tests.
cmake_minimum_required(VERSION 3.18)
project(nximport CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(NXIMPORT_TOOLS "the CLI and the tests" ON)
option(NXIMPORT_SANITIZE "build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(NXIMPORT_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

# mbedTLS: only the AES and SHA-256 sources (third_party/mbedtls, tag in docs/IMPORT-NSP-XCI.md), with our config
set(MBEDTLS ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/mbedtls)
if(NOT EXISTS ${MBEDTLS}/library/aes.c)
    message(FATAL_ERROR "third_party/mbedtls is missing: git submodule update --init third_party/mbedtls")
endif()
add_library(nximport_mbedtls STATIC
    ${MBEDTLS}/library/aes.c ${MBEDTLS}/library/aesce.c ${MBEDTLS}/library/aesni.c
    ${MBEDTLS}/library/sha256.c ${MBEDTLS}/library/platform_util.c)
target_include_directories(nximport_mbedtls PUBLIC ${MBEDTLS}/include ${CMAKE_CURRENT_SOURCE_DIR} PRIVATE ${MBEDTLS}/library)
target_compile_definitions(nximport_mbedtls PUBLIC MBEDTLS_CONFIG_FILE="mbedtls_config.h")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    target_compile_options(nximport_mbedtls PRIVATE -maes -mpclmul -msse2)
endif()

add_library(nximport STATIC
    src/io.cpp src/crypto.cpp src/keys.cpp src/container.cpp src/integrity.cpp src/nca.cpp src/romfs.cpp
    src/cnmt.cpp src/bktr.cpp src/nximport.cpp src/romfs_feed.cpp)
target_include_directories(nximport PUBLIC src)
find_package(Threads REQUIRED)
target_link_libraries(nximport PUBLIC nximport_mbedtls Threads::Threads)
target_compile_options(nximport PRIVATE -Wall -Wextra)

if(NXIMPORT_TOOLS)
    add_executable(nximport_cli cli/main.cpp)
    set_target_properties(nximport_cli PROPERTIES OUTPUT_NAME nximport)
    target_link_libraries(nximport_cli nximport)

    enable_testing()
    add_executable(nximport_tests tests/test_main.cpp tests/synth.cpp tests/test_crypto.cpp tests/test_keys.cpp
                   tests/test_containers.cpp tests/test_import.cpp tests/test_corrupt.cpp tests/test_update.cpp
                   tests/test_feed.cpp tests/test_big.cpp ${CMAKE_CURRENT_SOURCE_DIR}/../../runtime/romfs_build.c)
    # (runtime/romfs_build.c as it is: the app's romfs.bin builder, fed by RomfsFeed in test_feed.cpp; and the app's
    # transactional import, test_package_import.cpp)
    set(APP_CPP ${CMAKE_CURRENT_SOURCE_DIR}/../../android/totkrecompandroid/app/src/main/cpp)
    target_sources(nximport_tests PRIVATE tests/test_package_import.cpp ${APP_CPP}/package_import.cpp)
    target_include_directories(nximport_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../runtime ${APP_CPP})
    target_link_libraries(nximport_tests nximport)
    add_test(NAME nximport_tests COMMAND nximport_tests)
endif()
