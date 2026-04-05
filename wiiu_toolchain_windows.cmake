# =============================================================================
# 3SX Wii U Toolchain File for devkitPPC / WUT (MSYS2/Windows friendly)
# =============================================================================
# This alternative toolchain file is designed to work under MSYS2 on Windows.
# It avoids CMake list semantics causing stray semicolons by flattening
# compiler flags into a single string. Use it by passing
# `-DCMAKE_TOOLCHAIN_FILE=wiiu_toolchain_windows.cmake` when configuring.

# Verify that devkitPro is installed and the DEVKITPRO environment variable
# is available. The devkitPro packages provide a `devkit-env` script which
# should be sourced beforehand to set these variables. See the devkitPro
# documentation for installation instructions.
if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "DEVKITPRO environment variable not set.\n"
        "Make sure you have installed the Wii U toolchain (wut-linux) via pacman and\n"
        "sourced the devkitPro environment script (e.g. /etc/profile.d/devkit-env.sh).")
endif()

# Base directories. Note that CMake interprets semicolons as list separators,
# so use quoted strings for paths to avoid unintended list expansion.
set(DEVKITPRO "$ENV{DEVKITPRO}")
set(DEVKITPPC "${DEVKITPRO}/devkitPPC")
set(WUT_ROOT  "${DEVKITPRO}/wut")

# Identify the target system for CMake. This tells CMake not to use any host
# compiler when generating build scripts.
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc)
set(CMAKE_SYSTEM_VERSION 1)
set(WIIU TRUE)

# Define compilers explicitly. Quoting the paths helps avoid issues on MSYS2
# where forward slashes may be converted unexpectedly.
set(CMAKE_C_COMPILER   "${DEVKITPPC}/bin/powerpc-eabi-gcc")
set(CMAKE_CXX_COMPILER "${DEVKITPPC}/bin/powerpc-eabi-g++")
set(CMAKE_ASM_COMPILER "${DEVKITPPC}/bin/powerpc-eabi-gcc")
set(CMAKE_AR           "${DEVKITPPC}/bin/powerpc-eabi-ar"     CACHE FILEPATH "Archiver")
set(CMAKE_RANLIB       "${DEVKITPPC}/bin/powerpc-eabi-ranlib" CACHE FILEPATH "Ranlib")
set(CMAKE_STRIP        "${DEVKITPPC}/bin/powerpc-eabi-strip"  CACHE FILEPATH "Strip")

# Platform flags for the Wii U (Espresso / PPC750).
# It is crucial that this is specified as **a single argument**; passing multiple
# quoted arguments would turn this into a CMake list, and CMake writes lists
# with semicolons.  Semicolons in the cache become literal semicolons in the
# generated build script, which MSYS2 interprets as command separators.  Keep
# everything on one line here.
set(WIIU_C_FLAGS "-mcpu=750 -meabi -mhard-float -ffunction-sections -fdata-sections -D__WIIU__ -D__WUT__ -DTARGET_WIIU")

set(CMAKE_C_FLAGS_INIT   "${WIIU_C_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "${WIIU_C_FLAGS}")

# Explicitly set CMAKE_C_FLAGS and CMAKE_CXX_FLAGS in the cache.  Without
# this, CMake may populate the variables from other sources (e.g. previous
# cache entries) and reintroduce semicolons when the flags are treated as
# a list.  Using CACHE and FORCE ensures our flattened string is used.
set(CMAKE_C_FLAGS     "${WIIU_C_FLAGS}" CACHE STRING "C flags for Wii U" FORCE)
set(CMAKE_CXX_FLAGS   "${WIIU_C_FLAGS}" CACHE STRING "CXX flags for Wii U" FORCE)

# Linker flags. As with the compiler flags, specify the entire string as a
# single argument.  If multiple quoted arguments are passed, CMake will turn
# the variable into a list and write it with semicolons, which MSYS2 will
# misinterpret.  The FORCE option on CMAKE_EXE_LINKER_FLAGS replaces any
# cached value.
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-L${WUT_ROOT}/lib -L${DEVKITPPC}/lib -L${DEVKITPPC}/powerpc-eabi/lib -specs=${WUT_ROOT}/share/wut.specs -Wl,--gc-sections")
set(CMAKE_EXE_LINKER_FLAGS
    "-L${WUT_ROOT}/lib -L${DEVKITPPC}/lib -L${DEVKITPPC}/powerpc-eabi/lib -specs=${WUT_ROOT}/share/wut.specs -Wl,--gc-sections"
    CACHE STRING "Linker flags for Wii U" FORCE)

# Only search headers and libraries inside the devkitPro installation. This
# prevents CMake from accidentally picking up host headers on Windows.
set(CMAKE_FIND_ROOT_PATH
    "${DEVKITPRO}"
    "${DEVKITPPC}"
    "${DEVKITPPC}/powerpc-eabi"
    "${WUT_ROOT}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# WUT utilities for converting ELF → RPX. The default installation puts
# elf2rpl into ${WUT_ROOT}/bin.
set(WUT_ELF2RPX "${DEVKITPRO}/tools/bin/elf2rpl")

# Helper function: convert an ELF to an RPX after linking. CMake will call
# this automatically for any target that invokes `wiiu_create_rpx`.
function(wiiu_create_rpx target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${WUT_ELF2RPX} $<TARGET_FILE:${target}>
                $<TARGET_FILE_DIR:${target}>/${target}.rpx
        COMMENT "Converting ${target} ELF → RPX")
endfunction()