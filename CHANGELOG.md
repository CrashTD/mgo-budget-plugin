## v0.2.0 (2026-10-05)

- **Docs: memory cap.** `dxvk.maxMemoryBudget=13500` is tuned for 16 GB cards and needed: without it the frame rate collapses (13–19 fps instead of 34–35). `RADV_PERFTEST=nogttspill` does not replace it. README section "Memory cap", INI comment updated.
- **DevBench tools:** with the DevBench SKSE plugin installed, the plugin registers `mgobudget.status` (what it set and saw, including the last real DXGI budget and usage) and `mgobudget.set` (change the budget CSX sees while the game runs). Off with `[DevBench] Enabled=0`; nothing happens without DevBench.

## v0.1.0 (2026-10-03)

First release.

- **DXVK options without launch options:** appends `dxvk.maxMemoryBudget=13500;d3d11.cachedDynamicResources=c` to `DXVK_CONFIG` before the game starts its renderer.
- **Memory budget for Community Shaders:** raises the DXGI budget CSX checks for its VR render scale, so it keeps the scaled render targets under Proton (dock, 132 % / 90 Hz, FSR4 Ultra Performance: 37 fps without, 74 fps with the plugin).
- **Quest controller fix (opt-in):** idle Quest controllers no longer make VRIK set up Vive Wands with xrizer.
- **Health check:** logs known Proton pitfalls (Engine Fixes culling flag, old `d3dcompiler_47.dll`, missing `Data\data`, generic controller profile).
