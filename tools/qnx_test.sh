#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Cross-compiles the CarPlay->HUD unit tests for QNX 6.5 armle-v7 and EXECUTES them on real QNX 6.5
# under QEMU (luka-dev/qnx65-armv7-toolchain). Proves the translation layer builds and behaves the same
# on the target OS/CPU as on the host. Run inside WSL as root:
#   wsl -d Ubuntu-24.04 -u root -- bash -c "tr -d '\r' < .../carplay_hud/tools/qnx_test.sh | bash"
set -eu
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
TC=/root/qnx65-armv7-toolchain
VAR=${QNX_VARIANT:-8.5}
IMG=qnx65-armv7-toolchain:$VAR
P=arm-unknown-nto-qnx6.5.0eabi
TESTS="test_iap2_rgi test_cp_maneuver_map test_cp_rgi_state test_ident_patch"
OUT=/root/cp_qnx_build
mkdir -p "$OUT"

docker image inspect "$IMG" >/dev/null 2>&1 || { echo "$IMG not built yet"; exit 1; }

# Build in /work (a copy, so the Windows tree is only read).
rm -rf "$OUT/src" && mkdir -p "$OUT/src" && cp -r "$PROJ/src" "$PROJ/tests" "$PROJ/hook" "$OUT/src/"
docker run --rm --platform=linux/amd64 -v "$OUT":/w -w /w/src "$IMG" sh -c "
  set -e
  for t in $TESTS; do
    extra=
    [ \$t = test_ident_patch ] && extra=hook/ident_patch.c
    $P-g++ -std=gnu++98 -Wall -Wextra -Wno-long-long -Wno-ignored-attributes -O1 -static-libstdc++ -static-libgcc \
        tests/\$t.cpp \$extra -o /w/\$t
    echo built \$t: \$($P-readelf -h /w/\$t | grep -E 'Machine|Type' | tr -s ' ' | tr '\n' ' ')
  done
"

fail=0
for t in $TESTS; do
  echo "=== QNX 6.5 / QEMU: $t"
  res=$("$TC/tests/qemu-run.sh" "$OUT/$t" "$VAR" 2>&1) || true
  echo "$res" | tail -n 6
  echo "$res" | grep -q '0 failure(s)' || fail=1
done
[ $fail -eq 0 ] && echo "=== QNX OVERALL: PASS" || { echo "=== QNX OVERALL: FAIL"; exit 1; }
