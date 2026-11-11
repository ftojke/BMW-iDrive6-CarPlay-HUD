#!/usr/bin/env bash
# Build the QNX 6.5 armle-v7 hook .so using the qnx65-armv7-toolchain:8.5 Docker image.
set -eu
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
IMG=qnx65-armv7-toolchain:8.5
P=arm-unknown-nto-qnx6.5.0eabi
OUT=/root/cp_hook_build
mkdir -p "$OUT"
rm -rf "$OUT/src" && mkdir -p "$OUT/src"
cp -r "$PROJ/src" "$PROJ/hook" "$PROJ/tests" "$OUT/src/"

docker run --rm --platform=linux/amd64 -v "$OUT":/w -w /w/src "$IMG" sh -c '
set -e
P=arm-unknown-nto-qnx6.5.0eabi
CF="-std=gnu99 -Wall -Wextra -Wno-long-long -O1 -fPIC -ffunction-sections -fdata-sections"
XF="-std=gnu++98 -Wall -Wextra -Wno-long-long -Wno-ignored-attributes -O1 -fPIC -fno-exceptions -fno-rtti -fno-use-cxa-atexit -ffunction-sections -fdata-sections"
$P-gcc $CF -c hook/cp_hook.c      -o /w/cp_hook.o
$P-gcc $CF -c hook/cp_got_patch.c -o /w/cp_got_patch.o
$P-gcc $CF -c hook/cp_modinfo.c   -o /w/cp_modinfo.o
$P-gcc $CF -c hook/ident_patch.c  -o /w/ident_patch.o
$P-g++ $XF -c hook/cp_bridge.cpp  -o /w/cp_bridge.o
$P-g++ $XF -DCP_KOMBI_REAL -c hook/cp_kombi.cpp          -o /w/cp_kombi_real.o
$P-g++ $XF -DCP_KOMBI_REAL -c hook/kombi/kombi_client.cpp -o /w/kombi_client.o
LF="-shared -static-libstdc++ -static-libgcc -Wl,-Bsymbolic -Wl,--gc-sections"
$P-g++ $LF \
  /w/cp_hook.o /w/cp_got_patch.o /w/cp_modinfo.o /w/ident_patch.o /w/cp_bridge.o \
  /w/cp_kombi_real.o /w/kombi_client.o \
  -lsocket -o /w/libnbt_carplay_hud_real.so
echo "=== built. size: $(wc -c < /w/libnbt_carplay_hud_real.so)"
echo "=== NEEDED ==="; $P-readelf -d /w/libnbt_carplay_hud_real.so | grep NEEDED || echo none
echo "=== reloc type counts ==="; $P-readelf -r /w/libnbt_carplay_hud_real.so 2>/dev/null | awk "{for(i=1;i<=NF;i++) if(\$i ~ /^R_ARM/) print \$i}" | sort | uniq -c
echo "=== any REL32? ==="; $P-readelf -r /w/libnbt_carplay_hud_real.so 2>/dev/null | grep -c R_ARM_REL32 || true
echo "=== still-undefined C++ syms ==="; $P-nm -D -u /w/libnbt_carplay_hud_real.so | grep -E "_ZT|_ZN|_ZSt|__cxa|__gxx|__throw|exception" | head -20 || echo "(none)"
echo "=== exported dlopen? ==="; $P-nm -D /w/libnbt_carplay_hud_real.so | grep -E " T dlopen" && echo yes || echo NO
'
cp "$OUT/libnbt_carplay_hud_real.so" "$PROJ/build/libnbt_carplay_hud_real.so"
echo "=== copied to build/ ; host sha256:"
sha256sum "$PROJ/build/libnbt_carplay_hud_real.so"
