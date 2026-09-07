# PlayStation Vita build

The Vita port uses VitaSDK's SDL2 backend for video, controller, touch, audio,
and filesystem access. The current renderer stays on `SDL_Renderer`; the
existing lighting and post-processing effects operate on the small guest
framebuffer before SDL uploads it.

## Requirements

- VitaSDK with its environment variable `VITASDK` set
- VitaSDK packages for SDL2 and Ninja
- A Vita capable of running homebrew VPKs

## Build

From PowerShell at the repository root:

```powershell
cmake -S . -B build-vita -G Ninja "-DCMAKE_TOOLCHAIN_FILE=$env:VITASDK/share/vita.toolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DGBRECOMP_GENERATED_OPT_LEVEL=3 -DGBRECOMP_ENABLE_IPO=ON
cmake --build build-vita --parallel 12
```

The package is written to `build-vita/ResidentEvilGaiden.vpk`.

## ROM and writable data

The ROM is deliberately not included. Copy a legally acquired, matching dump
to either of these names under `ux0:/data/regaiden/` after installing the VPK:

```text
ux0:/data/regaiden/Resident Evil Gaiden (USA).gbc
ux0:/data/regaiden/rom.gbc
```

Required SHA-256:
`9a97678cbd8da02c8763e977674e17f460c06ea8b73bad35c52fe6817f506d44`.

Configuration, saves, savestates, optional `hd_pack/`, and optional
`music_pack/` also live below `ux0:/data/regaiden/`.

The front touch screen exposes the settings icon. Physical Vita controls are
handled through SDL's game-controller mapping.
