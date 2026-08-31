# PPSSPP Wii U Tiramisu runtime

This fork extends NicoAICP/PPSSPP-WIIU-FORWARDER, based on aliaspider's Wii U port.
The release branch retains runtime source commit
`62f03ca25e1d4fc689b3212b42b49024081bbca3`.
It is experimental and intended for **Tiramisu**, not Aroma.
Historical Nico-compatible builds reached Street Fighter Alpha 3 MAX gameplay
under Tiramisu; this is not proof that every game or newly compiled binary works.
LittleBigPlanet has hard-frozen the console in testing.

## Download and use

Download the runtime ZIP from this fork's Releases, not GitHub's Source code ZIP.
Keep `PPSSPP.rpx`, `PPSSPP.runtime.json` and the complete `assets/` directory
together on the computer. Select the RPX in UIF. The builder includes the assets
in the generated package. Follow its output instructions if you enable SD-copy.
The runtime is not a standalone installable title.

## Build

PowerShell 7 and Docker are required. From a fresh clone of this release branch:

```powershell
./scripts/Build-TiramisuRuntime.ps1
```

The wrapper configures a fresh Wii U build directory before invoking the
existing runtime builder with system-thread substitution OFF and
`TiramisuControl`. It does not build the Aroma experiment.
The source snapshot already vendors the required dependency trees; its old
.gitmodules entries are not an instruction to replace those trees with HEAD.

Distribute only the primary RPX, matching runtime manifest and complete assets
from `build-wiiu-autoboot-docker-20230621/`, together with LICENSE.TXT and
the retained dependency/asset notices. Preserve the generated source commit
and toolchain identity. Keep releases marked experimental.

## Existing binary

The August 21 TiramisuControl RPX has SHA-256
`39a853fc929247c270cc8a77546821c3f1e968e9d9b05c45184285bca93f390f`.
It was built from the runtime source commit above. This repository preparation
does not claim a new hardware pass or replace that binary.
