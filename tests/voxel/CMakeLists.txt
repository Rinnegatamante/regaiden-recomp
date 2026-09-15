cmake_minimum_required(VERSION 3.20)
project(ReGaidenVoxelTests LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../src/recompiled")
add_executable(voxel_tests
    voxel_tests.cpp
    ${SOURCE_ROOT}/voxelizer.cpp
    ${SOURCE_ROOT}/config_ini.c
    ${SOURCE_ROOT}/voxel/gaiden_map.cpp
    ${SOURCE_ROOT}/voxel/profiles.cpp
    ${SOURCE_ROOT}/voxel/rasterizer.cpp
    ${SOURCE_ROOT}/voxel/scene.cpp
    ${SOURCE_ROOT}/runtime/src/gbrt_data_mod.c
    ${SOURCE_ROOT}/runtime/src/gbrt_hash.c
)
target_include_directories(voxel_tests PRIVATE ${SOURCE_ROOT} ${SOURCE_ROOT}/runtime/include)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(voxel_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
enable_testing()
add_test(NAME voxel_regressions COMMAND voxel_tests)
