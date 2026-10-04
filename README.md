# MGO Budget Plugin

Linux/Proton fixes for Skyrim VR as a single SKSE plugin. Made for Mad God Overhaul (MGO), but nothing in it is MGO-specific.

> Not a coder – Claude wrote most of it, I only tested it on my rig. If it sets your PC on fire, blame the robot. 😉

## What it does

Everything only runs under Wine/Proton. On Windows the plugin loads and does nothing.

| | Problem under Proton | What the plugin does | Replaces |
|---|---|---|---|
| **Memory budget** | Community Shaders (CSX) checks the DXGI memory budget before it switches to its scaled VR render targets (Render Scale / FSR4). Under Proton the reported budget is too low: on RADV it excludes memory of other processes, while the usage also counts Proton's separate FSR4 device. CSX sees "Critical" and silently stays at native resolution. | Raises the budget CSX sees (only the budget, the real usage stays). | the [budget layer](https://github.com/CrashTD/mgo-budget-layer) |
| **DXVK options** | `d3d11.cachedDynamicResources=c` (+18 to +33 % with FSR4) and `dxvk.maxMemoryBudget` had to go into the launch options. | Sets them before the game starts its renderer. The memory cap is tuned for 16 GB cards, see [Memory cap](#memory-cap). | `DXVK_CONFIG` in the launch options |
| **Quest controllers** (opt-in) | If Quest controllers lie still or sleep while the save loads, WiVRn reports a generic profile, xrizer answers `<unknown>` and VRIK sets up Vive Wands: sticks and face buttons stay dead. | Reports those controllers as Oculus Touch. **Quest headsets only.** | the [xrizer fork](https://github.com/CrashTD/xrizer/tree/quest-vrik-controllers) |
| **Health check** | Several known pitfalls fail silently. | Logs warnings for: Engine Fixes `bCullingFreedObjectCrash = true` (black screen), the 2013 `d3dcompiler_47.dll` from winetricks (CSX shaders fail), missing `Data\data` (slow startup), the generic controller profile. | checking by hand |
| **DevBench tools** (only with [DevBench](https://github.com/alandtse/devbench)) | Checking what the plugin did means reading its log after the game. | Registers `mgobudget.status` (DXVK options, budget hook with the last real DXGI values, controller hook, health check) and `mgobudget.set` (memory budget live) with DevBench's local endpoint: `curl -X POST 127.0.0.1:8921/api/tool/mgobudget.status`. Does nothing without DevBench. | reading the log |

## Result

Skyrim VR (MGO), RX 9070 XT, Quest 3 via WiVRn/xrizer at 2724x2853 per eye (WiVRn 132 % = VD Ultra), 90 Hz, FSR4 Ultra Performance, same spot, launch options without layer and without `DXVK_CONFIG`:

| | fps |
|---|---|
| without the plugin | 37 (CSX stuck at native resolution) |
| **with the plugin** | **74** |

Quest controllers idle while loading: with xrizer without the controller fix VRIK reported `Vive Wands` without the plugin and `Oculus Rift controllers` with it; with stock xrizer (headset reported as `<unknown>`) and the plugin, also `Oculus Rift controllers`.

## Install

1. Download `MGOBudgetPlugin-<version>.zip` from [Releases](../../releases) and install it as a mod in Mod Organizer 2 (it contains `SKSE/Plugins/MGOBudgetPlugin.dll` and `.ini`).
2. If your launch options contain `DXVK_CONFIG=…`, remove it: the plugin sets these options now. If you used the budget layer, remove its variables too.
3. **Quest owners:** set `QuestFixGenericProfile=1` in `MGOBudgetPlugin.ini`.
4. Start the game once and look at `MGOBudgetPlugin.log` (with MO2 in `overwrite/SKSE/Plugins/`): it shows what was set and the health check results.

## Settings (`MGOBudgetPlugin.ini`)

| Section | Key | Default | Meaning |
|---|---|---|---|
| `[DXVK]` | `Config` | `dxvk.maxMemoryBudget=13500;d3d11.cachedDynamicResources=c` | appended to `DXVK_CONFIG` (later entries win). `maxMemoryBudget` in MiB: 13500 is tuned for 16 GB cards and needed, see [Memory cap](#memory-cap). |
| `[Budget]` | `MiB` | `24000` | budget CSX sees for its render scale check, `0` = off |
| `[Controller]` | `QuestFixGenericProfile` | `0` | `1` = report idle Quest controllers as Touch. Stays off if the headset clearly reports another vendor. |
| `[Check]` | `Enabled` | `1` | health check at startup |
| `[DevBench]` | `Enabled` | `1` | register the DevBench tools (only if DevBench is installed) |

## Memory cap

`dxvk.maxMemoryBudget=13500` is not just a tweak, it is needed. It keeps DXVK below the point where a 16 GB card runs out of room next to the desktop, the VR runtime and Proton's FSR4 device. Above that point RADV lets the kernel move video memory to system RAM behind DXVK's back (the buffers are created as "VRAM, may spill to GTT", Mesa `radv_amdgpu_bo.c`). DXVK still counts that memory as video memory, cannot react, and the frame rate collapses.

Measured on an RX 9070 XT (16 GB), KDE Plasma, proton-cachyos, FSR4, 132 % / 90 Hz:

| Spot | cap 13500 | no cap |
|---|---|---|
| City (earlier test) | 37–45 fps | 11–12 fps |
| Checkpoint, FSR4 Balanced, Radium off, `RADV_PERFTEST=nogttspill` | 34 fps | 13 fps |
| City, FSR4 Balanced, Radium off, `RADV_PERFTEST=nogttspill` | 35 fps | 19 fps |

- **16 GB:** keep 13500. Do not raise it and do not set `0`.
- **`RADV_PERFTEST=nogttspill`** (stops the hidden moving) does not help with stock DXVK: with the cap the frame rate stayed the same (34 / 35 fps), without the cap it collapsed as well. Worse, with a full card the kernel then logs `Not enough memory for command submission` and fails to pin the desktop's framebuffer; once the game lost its GPU device (crash). Not recommended.
- **Other card sizes (untested, I only have a 16 GB card):** the cap leaves about 2.8 GB for everything else on a 16 GB card. On bigger cards 13500 probably holds DXVK back, try card size minus about 3 GB. On smaller cards the driver's own budget is lower than 13500 and the cap does nothing.

## Limits

- Tested with Skyrim VR 1.4.15, SKSE VR 2.0.12, CSX 3.19.1, proton-cachyos (native), WiVRn + xrizer, RX 9070 XT. Other setups are untested.
- The controller fix is for Quest headsets. Stock xrizer does not tell the plugin which headset is connected (tested: it reports `<unknown>`), which is why it is opt-in.
- The budget only changes what CSX sees. If your card really runs out of memory, DXVK's own cap (`dxvk.maxMemoryBudget`) still decides, see [Memory cap](#memory-cap).

## Build

Needs `x86_64-w64-mingw32-gcc` (and `wine`, `clang-format`, `clang-tidy` for the checks).

```
make            # MGOBudgetPlugin.dll
make test       # loads the plugin under Wine with an SKSE stand-in
make dist       # MO2-ready zip
```

## License

GPL-3.0-or-later, see `LICENSE`.
