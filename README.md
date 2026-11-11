# BMW iDrive 6 (NBT Evo) - Native Apple CarPlay HUD & Cluster Turn-by-Turn

Native **Apple CarPlay** turn-by-turn navigation (**Apple Maps** and **Google Maps**) on the **Head-Up Display (HUD)** and **digital instrument cluster (KOMBI)** for BMW **NBT Evo ID6** head units.

Arrows, distance, and street names appear automatically on the HUD and cluster whenever a CarPlay route is active, and clear as soon as the route ends.

---

## Disclaimer - Use at Your Own Risk

**I am not responsible for anything you do to your car or head unit. Execute commands and modify files at your own risk - if you break something, only BMW can reflash your head unit. This software is provided as-is, with no warranty.**

- **Tested ONLY on a BMW G30 (`NBTevo_Y21432J`) with a 6WB digital cluster and HUD.**
- No support is provided for bench setups, retrofits, or other firmware versions.
- If your head unit has a different `ipod-drvr-iap2.so` binary, the hook automatically disables itself and runs 100% stock.

---

## License

Licensed under the **PolyForm Noncommercial License 1.0.0** ([`LICENSE`](LICENSE)).

**This project is free for personal, non-commercial use. Selling it, charging for installation, or bundling it with paid products/services is strictly prohibited.** If you paid for this, you were scammed.

*(KOMBI SOME/IP output is adapted from [NBT-AAD](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto); iAP2 Route Guidance TLV definitions are based on [mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi).)*

---

## Supported Apps

| App | Status | Notes |
|---|---|---|
| **Apple Maps** | **Working** | Full turn-by-turn on HUD & cluster |
| **Google Maps** | **Working** | Full turn-by-turn on HUD & cluster |
| **Waze** (iOS) | **Not supported by Waze** | iOS Waze currently sends `sourceSupportsRouteGuidance = 0` over iAP2 (no maneuver data). Will work automatically if Waze enables it in a future iOS update. |

---

## Installation

See **[`install.md`](install.md)** for the step-by-step USB + SSH installation and rollback guide.