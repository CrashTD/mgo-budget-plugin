# MGO Budget Plugin

Linux/Proton fixes for Skyrim VR as a single SKSE plugin. Made for Mad God Overhaul (MGO), but nothing in it is MGO-specific.

> Not a coder – Claude wrote most of it, I only tested it on my rig. If it sets your PC on fire, blame the robot. 😉

## What it does

Everything only runs under Wine/Proton. On Windows the plugin loads and does nothing.

| | Problem under Proton | What the plugin does | Replaces |
|---|---|---|---|
| **Memory budget** | Community Shaders (CSX) checks the DXGI memory budget before it switches to its scaled VR render targets (Render Scale / FSR4). Under Proton the reported budget is too low: on RADV it excludes memory of other processes, while the usage also counts Proton's separate FSR4 device. CSX sees "Critical" and silently stays at native resolution. | Raises the budget CSX sees (only the budget, the real usage stays). | the [budget layer](https://github.com/CrashTD/mgo-budget-layer) |
| **DXVK options** | `d3d11.cachedDynamicResources=c` (+18 to +33 % with FSR4) and `dxvk.maxMemoryBudget` had to go into the launch options. | Sets them before the game starts its renderer. | `DXVK_CONFIG` in the launch options |
| **Quest controllers** (opt-in) | If Quest controllers lie still or sleep while the save loads, WiVRn reports a generic profile, xrizer answers `<unknown>` and VRIK sets up Vive Wands: sticks and face buttons stay dead. | Reports those controllers as Oculus Touch. **Quest headsets only.** | the [xrizer fork](https://github.com/CrashTD/xrizer/tree/quest-vrik-controllers) |
| **Health check** | Several known pitfalls fail silently. | Logs warnings for: Engine Fixes `bCullingFreedObjectCrash = true` (black screen), the 2013 `d3dcompiler_47.dll` from winetricks (CSX shaders fail), missing `Data\data` (slow startup), the generic controller profile. | checking by hand |

## Result

Skyrim VR (MGO), RX 9070 XT, Quest 3 via WiVRn/xrizer at 2724x2853 per eye (WiVRn 132 % = VD Ultra), 90 Hz, FSR4 Ultra Performance, same spot, launch options without layer and without `DXVK_CONFIG`:

| | fps |
|---|---|
| without the plugin | 37 (CSX stuck at native resolution) |
| **with the plugin** | **74** |

Quest controllers with an xrizer build without the controller fix, controllers idle while loading: VRIK reported `Vive Wands` without the plugin, `Oculus Rift controllers` with it.

## Install

1. Download `MGOBudgetPlugin-<version>.zip` from [Releases](../../releases) and install it as a mod in Mod Organizer 2 (it contains `SKSE/Plugins/MGOBudgetPlugin.dll` and `.ini`).
2. Remove from your launch options what the plugin now does: the budget layer variables (`MGO_BUDGET_FAKE_*`, `VK_ADD_LAYER_PATH`, `VK_LOADER_LAYERS_ENABLE`) and `DXVK_CONFIG=…`. Keep the rest, e.g. `PROTON_FSR4_UPGRADE=1 %command%`.
3. **Quest owners:** set `QuestFixGenericProfile=1` in `MGOBudgetPlugin.ini`.
4. Start the game once and look at `MGOBudgetPlugin.log` (with MO2 in `overwrite/SKSE/Plugins/`): it shows what was set and the health check results.

## Settings (`MGOBudgetPlugin.ini`)

| Section | Key | Default | Meaning |
|---|---|---|---|
| `[DXVK]` | `Config` | `dxvk.maxMemoryBudget=13500;d3d11.cachedDynamicResources=c` | appended to `DXVK_CONFIG` (later entries win). 13500 is the ceiling on a 16 GB card; more and the city stalls. |
| `[Budget]` | `MiB` | `24000` | budget CSX sees for its render scale check, `0` = off |
| `[Controller]` | `QuestFixGenericProfile` | `0` | `1` = report idle Quest controllers as Touch. Stays off if the headset clearly reports another vendor. |
| `[Check]` | `Enabled` | `1` | health check at startup |

## Limits

- Tested with Skyrim VR 1.4.15, SKSE VR 2.0.12, CSX 3.19.1, proton-cachyos (native), WiVRn + xrizer, RX 9070 XT. Other setups are untested.
- The controller fix is for Quest headsets. Stock xrizer does not tell the plugin which headset is connected, which is why it is opt-in.
- The budget only changes what CSX sees. If your card really runs out of memory, DXVK's own cap (`dxvk.maxMemoryBudget`) still decides.

## Build

Needs `x86_64-w64-mingw32-gcc` (and `wine`, `clang-format`, `clang-tidy` for the checks).

```
make            # MGOBudgetPlugin.dll
make test       # loads the plugin under Wine with an SKSE stand-in
make dist       # MO2-ready zip
```

## License

GPL-3.0-or-later, see `LICENSE`.
