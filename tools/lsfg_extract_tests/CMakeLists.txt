# Host tests of the frame generation's shader extraction (docs/FRAMEGEN.md § B1).
#   cmake -S tools/lsfg_extract_tests -B build/lsfg -DLSFG_SANITIZE=ON && cmake --build build/lsfg && build/lsfg/lsfg_extract_tests
cmake_minimum_required(VERSION 3.18)
project(lsfg_extract_tests CXX C)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(LSFG_SANITIZE "AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(LSFG_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

set(R ${CMAKE_CURRENT_SOURCE_DIR}/../..)
set(APP_CPP ${R}/android/totkrecompandroid/app/src/main/cpp)
set(NXIMPORT_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory(${R}/tools/nximport ${CMAKE_BINARY_DIR}/nximport)
include(${APP_CPP}/lsfg_deps.cmake)

add_executable(lsfg_extract_tests test_lsfg_extract.cpp ${APP_CPP}/lsfg_extract.cpp ${APP_CPP}/lsfg_translate.cpp)
target_include_directories(lsfg_extract_tests PRIVATE ${APP_CPP})
target_link_libraries(lsfg_extract_tests lsfg_deps nximport)
target_compile_definitions(lsfg_extract_tests PRIVATE ${LSFG_DEFS})
