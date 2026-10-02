## v0.1.0 (2026-10-03)

First release.

- **DXVK options without launch options:** appends `dxvk.maxMemoryBudget=13500;d3d11.cachedDynamicResources=c` to `DXVK_CONFIG` before the game starts its renderer.
- **Memory budget for Community Shaders:** raises the DXGI budget CSX checks for its VR render scale, so it keeps the scaled render targets under Proton (dock, 132 % / 90 Hz, FSR4 Ultra Performance: 37 fps without, 74 fps with the plugin).
- **Quest controller fix (opt-in):** idle Quest controllers no longer make VRIK set up Vive Wands with xrizer.
- **Health check:** logs known Proton pitfalls (Engine Fixes culling flag, old `d3dcompiler_47.dll`, missing `Data\data`, generic controller profile).
