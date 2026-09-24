#!/usr/bin/env bash
#
# Runs emlPhdFixEndian() over the real PHD sound banks on an emulated
# big-endian PowerPC and diffs what the sound driver would read against a
# little-endian reference parse of the same bytes. They must agree exactly.
#
#   sudo apt-get install -y gcc-powerpc-linux-gnu qemu-user-static libsdl2-dev
#   tools/be-tests/run.sh
#
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
STAGE="$ROOT/.be-check"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

[ -d "$STAGE" ] || "$ROOT/tools/check-bigendian.sh" --setup

powerpc-linux-gnu-gcc -std=gnu11 -O2 -Wall -static \
    -DSDL_DISABLE_IMMINTRIN_H -DTARGET_WIIU -D__WIIU__ \
    -Wno-stringop-overflow -Wno-array-bounds \
    -I"$ROOT/src" -I"$ROOT/include" -I"$ROOT/include/sdk" -I"$STAGE" \
    -o "$OUT/phd_test" \
    "$ROOT/tools/be-tests/phd_endian_test.c" \
    "$ROOT/src/sf33rd/AcrSDK/MiddleWare/PS2/CapSndEng/emlSndEndian.c" \
    "$ROOT/src/sf33rd/AcrSDK/MiddleWare/PS2/CapSndEng/eflSpuMap.c" \
    "$ROOT/src/sf33rd/Source/PS2/cseDataFiles/SpuMap.c" \
    "$ROOT/src/sf33rd/Source/PS2/cseDataFiles/PHD_SE.c" \
    "$ROOT/src/sf33rd/Source/PS2/cseDataFiles/PHD_PL00.c"

qemu-ppc-static "$OUT/phd_test" | grep -v '^endianness' > "$OUT/got.txt"
(cd "$ROOT" && python3 tools/be-tests/phd_endian_ref.py) > "$OUT/ref.txt"

python3 - "$OUT/ref.txt" "$OUT/got.txt" <<'PY'
import re, sys
def vals(p):
    return set(re.findall(r'[A-Za-z]\w*=(?:0x)?-?\w+', open(p).read()))
ref, got = vals(sys.argv[1]), vals(sys.argv[2])
missing = sorted(ref - got)
if missing:
    print("MISMATCH; little-endian reference values not reproduced:", missing)
    sys.exit(1)
print("PHD endian fixup: big-endian result matches little-endian reference (%d values)" % len(ref))
PY
