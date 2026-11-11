#!/usr/bin/env bash
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0
# Vendors the car-verified KOMBI output stack from nanaisu's NBT-AAD into hook/kombi/ for the CarPlay->HUD
# real build (CP_KOMBI_REAL). These files stay under their original PolyForm Noncommercial license and keep
# their copyright headers; we only reuse them unchanged to drive SOME/IP 0xB006 (cluster + HUD).
#
# Only the files our adapter (hook/cp_kombi.cpp) needs: Graph (kombi_graph) + KombiClient (kombi_client),
# plus their headers. NOT the Android-Auto Core (kombi_nav.*), which we replace with our CarPlay logic.
#
# usage:  bash tools/vendor_kombi.sh          (run from the project root; needs curl + network)
set -eu
# Run from the project root (carplay_hud). DEST is relative to the current directory, which is robust
# whether the script is executed directly or piped to bash.
DEST="$PWD/hook/kombi"
[ -d "$PWD/hook" ] || { echo "run from the carplay_hud project root (no ./hook here)"; exit 1; }
REF="${NBT_AAD_REF:-main}"
BASE="https://raw.githubusercontent.com/Sq00ky/Supra-iDrive6-Android-Auto/$REF/milestone-7/src"
mkdir -p "$DEST"

FILES="kombi_client.hpp kombi_client.cpp kombi_graph.hpp kombi_graph_template.hpp kombi_nav_msg.hpp"
for f in $FILES; do
    echo "fetching $f"
    curl -fsSL "$BASE/$f" -o "$DEST/$f"
    head -2 "$DEST/$f" | grep -q PolyForm || { echo "WARN: $f missing license header"; }
done
echo "vendored into $DEST:"
ls -l "$DEST"
cat > "$DEST/README.md" <<EOF
# Vendored KOMBI output stack (from NBT-AAD)

These files are copied **unchanged** from nanaisu's NBT-AAD
(github.com/Sq00ky/Supra-iDrive6-Android-Auto, milestone-7/src), ref \`$REF\`, under their
**PolyForm Noncommercial License 1.0.0**. They build the BMW KOMBI route-guidance graph (SOME/IP 0xB006)
that draws the arrow on the cluster and HUD - car-verified on NBTevo_Y21432J (issue #75).

Our CarPlay hook reuses only \`Graph\` (kombi_graph) and \`KombiClient\` (kombi_client); the
Android-Auto Core (kombi_nav.*) is replaced by our CarPlay RGI logic (src/cp_rgi_state.hpp).

Re-fetch with: \`bash tools/vendor_kombi.sh\`. Do not edit here; change upstream.
EOF
