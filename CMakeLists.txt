# Resident Evil Gaiden - Native C Recompilation
# Root CMake Configuration
cmake_minimum_required(VERSION 3.20)
project(ResidentEvilGaidenRecomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(VITA)
    list(PREPEND CMAKE_PREFIX_PATH "${VITASDK}/arm-vita-eabi")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL2-2.30.12")
    list(APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/SDL2-2.30.12")
endif()

if(WIN32)
    if(MSVC)
        add_link_options(/STACK:67108864)
    else()
        add_link_options(-Wl,--stack,67108864)
    endif()
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# Include the recompiled game sources
add_subdirectory(src/recompiled)

if(VITA)
    include("${VITASDK}/share/vita.cmake" REQUIRED)

    set(REGAIDEN_VITA_TITLE_ID "RESGAIDEN" CACHE STRING "PS Vita title ID")
    set(REGAIDEN_VITA_APP_NAME "Resident Evil: Gaiden" CACHE STRING "PS Vita LiveArea title")
    set(REGAIDEN_VITA_VERSION "01.00" CACHE STRING "PS Vita application version")

    set(REGAIDEN_VITA_ELF
        "${CMAKE_CURRENT_BINARY_DIR}/src/recompiled/Resident_Evil_Gaiden__USA_")
    vita_create_self(eboot.bin "${REGAIDEN_VITA_ELF}" NOASLR)
    add_dependencies(eboot.bin-self Resident_Evil_Gaiden__USA_)

    set(REGAIDEN_VITA_VPK_FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/config.default.ini" "config.default.ini")
    file(GLOB_RECURSE REGAIDEN_VITA_SCE_SYS_FILES
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/*")
    foreach(asset IN LISTS REGAIDEN_VITA_SCE_SYS_FILES)
        list(APPEND REGAIDEN_VITA_VPK_FILES
            "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" "${asset}")
    endforeach()

    vita_create_vpk(
        ResidentEvilGaiden.vpk
        "${REGAIDEN_VITA_TITLE_ID}"
        eboot.bin
        VERSION "${REGAIDEN_VITA_VERSION}"
        NAME "${REGAIDEN_VITA_APP_NAME}"
        FILE ${REGAIDEN_VITA_VPK_FILES}
    )
    add_dependencies(ResidentEvilGaiden.vpk-vpk eboot.bin-self)
endif()

