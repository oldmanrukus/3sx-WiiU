# =============================================================================
# 3SX Wii U Toolchain File for devkitPPC / WUT
# =============================================================================
# Usage:
#   cmake -B build-wiiu -DCMAKE_TOOLCHAIN_FILE=wiiu_toolchain.cmake \
#         -DCMAKE_BUILD_TYPE=Release
#   cmake --build build-wiiu
#
# Requires:
#   - devkitPro with devkitPPC installed
#   - WUT (Wii U Toolchain) installed via: (dkp-)pacman -S wut-linux
#   - DEVKITPRO environment variable set (e.g. /opt/devkitpro)
# =============================================================================

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO environment variable not set. "
        "Install devkitPro and source the environment: "
        "export DEVKITPRO=/opt/devkitpro")
endif()

set(DEVKITPRO  $ENV{DEVKITPRO})
set(DEVKITPPC  ${DEVKITPRO}/devkitPPC)
set(WUT_ROOT   ${DEVKITPRO}/wut)

# System identification
set(CMAKE_SYSTEM_NAME       Generic)
set(CMAKE_SYSTEM_PROCESSOR  powerpc)
set(CMAKE_SYSTEM_VERSION    1)
set(WIIU TRUE)

# Compilers
set(CMAKE_C_COMPILER   ${DEVKITPPC}/bin/powerpc-eabi-gcc)
set(CMAKE_CXX_COMPILER ${DEVKITPPC}/bin/powerpc-eabi-g++)
set(CMAKE_ASM_COMPILER ${DEVKITPPC}/bin/powerpc-eabi-gcc)
set(CMAKE_AR           ${DEVKITPPC}/bin/powerpc-eabi-ar CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB       ${DEVKITPPC}/bin/powerpc-eabi-ranlib CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP        ${DEVKITPPC}/bin/powerpc-eabi-strip CACHE FILEPATH "Strip")

# Platform flags — Wii U (Espresso / PPC750)
set(WIIU_C_FLAGS
    "-mcpu=750 -meabi -mhard-float "
    "-ffunction-sections -fdata-sections "
    "-D__WIIU__ -D__WUT__ "
    "-DTARGET_WIIU "
    "-isystem ${WUT_ROOT}/include "
    "-isystem ${DEVKITPPC}/powerpc-eabi/include "
)

set(CMAKE_C_FLAGS_INIT   ${WIIU_C_FLAGS})
set(CMAKE_CXX_FLAGS_INIT ${WIIU_C_FLAGS})

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-L${WUT_ROOT}/lib -L${DEVKITPPC}/lib -L${DEVKITPPC}/powerpc-eabi/lib "
    "-specs=${WUT_ROOT}/share/wut.specs "
    "-Wl,--gc-sections"
)

# Search paths — only look in devkitPro, never host
set(CMAKE_FIND_ROOT_PATH
    ${DEVKITPRO}
    ${DEVKITPPC}
    ${DEVKITPPC}/powerpc-eabi
    ${WUT_ROOT}
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# WUT utilities for RPX/WUHB generation
set(WUT_ELF2RPX ${WUT_ROOT}/bin/elf2rpl)

# Helper function: convert ELF → RPX and optionally package as WUHB
function(wiiu_create_rpx target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${WUT_ELF2RPX} $<TARGET_FILE:${target}>
                $<TARGET_FILE_DIR:${target}>/${target}.rpx
        COMMENT "Converting ${target} ELF → RPX"
    )
endfunction()
