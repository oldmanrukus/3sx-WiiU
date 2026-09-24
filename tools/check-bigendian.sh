#!/usr/bin/env bash
#
# Type-checks the Wii U sources for a big-endian 32-bit PowerPC target without
# devkitPPC. It will not produce an RPX — it catches compile errors and
# endianness-sensitive warnings on a machine where the real toolchain is not
# available (the devkitPro package host is often unreachable from CI).
#
# Setup (Debian/Ubuntu):
#   sudo apt-get install -y gcc-powerpc-linux-gnu libsdl2-dev qemu-user-static
#   tools/check-bigendian.sh --setup      # stages headers into .be-check/
#   tools/check-bigendian.sh              # check every file
#   tools/check-bigendian.sh src/foo.c    # check one file
#
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STAGE="$ROOT/.be-check"
CC=powerpc-linux-gnu-gcc

# Files that need real wut/GX2/SDL3/ffmpeg headers, or that the Wii U build
# does not compile at all.
SKIP='src/port/config/keymap.c
src/port/io/afs.c
src/port/resources.c
src/port/sdl/sdl_app.c
src/port/sdl/sdl_pad.c
src/port/sound/adx.c
src/port/sound/spu.c
src/port/wiiu/wiiu_game_renderer.c
src/port/wiiu/wiiu_app.c
src/port/wiiu/wiiu_shaders.c'

setup() {
    command -v "$CC" >/dev/null || { echo "missing $CC (apt install gcc-powerpc-linux-gnu)"; exit 1; }
    [ -d /usr/include/SDL2 ] || { echo "missing SDL2 headers (apt install libsdl2-dev)"; exit 1; }
    rm -rf "$STAGE"
    mkdir -p "$STAGE/SDL2" "$STAGE/coreinit"
    cp /usr/include/SDL2/*.h "$STAGE/SDL2/"
    # SDL_config.h redirects into the multiarch dir; inline it so the cross
    # compiler never sees the host's glibc headers.
    for d in /usr/include/*-linux-gnu/SDL2/_real_SDL_config.h; do
        [ -f "$d" ] && cp "$d" "$STAGE/SDL2/"
    done
    sed -i 's|#include <SDL2/_real_SDL_config.h>|#include "_real_SDL_config.h"|' "$STAGE/SDL2/SDL_config.h"
    cat > "$STAGE/coreinit/debug.h" <<'HDR'
#ifndef STUB_COREINIT_DEBUG_H
#define STUB_COREINIT_DEBUG_H
void OSReport(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void OSFatal(const char* msg);
#endif
HDR
    echo "staged headers in $STAGE"
}

check_one() {
    "$CC" -fsyntax-only -std=gnu11 \
        -DSDL_DISABLE_IMMINTRIN_H \
        -DTARGET_WIIU -D__WIIU__ -D__WUT__ -D_POSIX_C_SOURCE \
        -DMEMCARD_DISABLED -DNETPLAY_STUB \
        -I"$ROOT/src" -I"$ROOT/include" -I"$ROOT/include/sdk" -I"$STAGE" \
        -Wall -fno-strict-aliasing \
        -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable \
        -Wno-pointer-sign -Wno-parentheses -Wno-incompatible-pointer-types \
        -Wno-discarded-qualifiers -Wno-format-overflow -Wno-comment \
        -Wno-maybe-uninitialized -Wno-array-bounds -Wno-aggressive-loop-optimizations \
        -Wno-stringop-overflow -Wno-restrict -Wno-stringop-truncation \
        "$1" 2>&1
}

[ "${1-}" = "--setup" ] && { setup; exit 0; }
[ -d "$STAGE" ] || setup

if [ $# -gt 0 ]; then
    fail=0
    for f in "$@"; do
        out=$(check_one "$f")
        [ -n "$out" ] && { echo "=== $f"; echo "$out"; fail=1; }
    done
    exit $fail
fi

ok=0; fail=0
while read -r f; do
    grep -qxF "$f" <<<"$SKIP" && continue
    out=$(check_one "$f")
    if [ -z "$out" ]; then
        ok=$((ok + 1))
    else
        fail=$((fail + 1)); echo "=== $f"; echo "$out"
    fi
done < <(cd "$ROOT" && find src/sf33rd src/arcade src/rendering src/bin2obj src/stb src/argparse src/port -name '*.c' | sort)

echo "big-endian check: $ok clean, $fail with diagnostics"
[ "$fail" -eq 0 ]
