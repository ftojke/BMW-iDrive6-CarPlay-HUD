# Vendored KOMBI output stack (from NBT-AAD)

These files are copied **unchanged** from nanaisu's NBT-AAD
(github.com/Sq00ky/Supra-iDrive6-Android-Auto, milestone-7/src), ref `main`, under their
**PolyForm Noncommercial License 1.0.0**. They build the BMW KOMBI route-guidance graph (SOME/IP 0xB006)
that draws the arrow on the cluster and HUD - car-verified on NBTevo_Y21432J (issue #75).

Our CarPlay hook reuses only `Graph` (kombi_graph) and `KombiClient` (kombi_client); the
Android-Auto Core (kombi_nav.*) is replaced by our CarPlay RGI logic (src/cp_rgi_state.hpp).

Re-fetch with: `bash tools/vendor_kombi.sh`. Do not edit here; change upstream.
