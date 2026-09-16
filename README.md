# Resident Evil Gaiden: Recompiled (PS Vita)

A native static recompilation of **Resident Evil Gaiden** (Game Boy Color, 2001) for PlayStation Vita, built in C/C++ on top of SDL2 and Dear ImGui.

The Vita port runs the recompiled game code natively and adds optional enhancements including **Item Sparkles**, **Widescreen**, **Dynamic Flashlight Lighting**, **Atmospheric Post-Processing**, **Savestates**, **Cheats**, **HD Texture Packs**, and **Replacement Music Packs**.

> **Default presentation:** the game starts close to the original Game Boy Color presentation: native 10:9 aspect ratio, native GBC colours, and optional visual enhancements disabled. Settings can be changed from the in-game menu with **Square** and are saved to `ux0:data/regaiden/config.ini`.

## Features

### Item Sparkles
Pickable items and zombie drops can display a sparkle while they are visible, making exploration less dependent on checking every tile manually.

### Widescreen
The optional widescreen renderer extends the visible room horizontally beyond the original 160x144 Game Boy Color viewport while preserving the original PPU-rendered image in the centre.

- Native mode: **160x144**
- Widescreen framebuffer: **256x144**
- Extended entity rendering and room-edge handling
- Vita-optimized rendering path

### Dynamic Flashlight Lighting
Barry's flashlight can cast a directional light cone over exploration scenes.

- Direction follows player movement
- Configurable flashlight intensity and ambient darkness
- Optional light flicker

### Atmospheric Post-Processing
Optional effects are available from the in-game menu:

- Vignette
- Film grain
- CRT scanlines
- CRT phosphor mask
- Multiple colour-grading profiles

The Vita build uses a hardware-accelerated post-processing path for these effects.

### HD Texture Pack Support
Optional PNG replacements can be loaded from:

```text
ux0:data/regaiden/hd_pack/
```

Supported folders include:

```text
hd_pack/
  backgrounds/
  monsters/
  portraits/
```

PNG transparency is supported. HD assets can be reloaded from the in-game settings menu without restarting the game.

The bundled/sample HD pack is only a proof of concept and is not intended to represent a finished art direction.

### Replacement Soundtrack
Custom `.ogg` or `.wav` tracks can be placed in:

```text
ux0:data/regaiden/music_pack/
```

Files use the naming convention:

```text
track_<id>.ogg
```

For example, `track_2.ogg` replaces music ID 2. Missing replacement tracks continue using the original Game Boy Color audio. The original APU audio can be ducked rather than completely muted so sound effects remain audible.

### Cheats and GameShark Codes
The in-game menu includes built-in cheats and support for Game Boy Color GameShark codes.

Built-in options include:

- Infinite Health
- Infinite Ammo
- Unlock All Weapons
- Infinite Items
- Freeze Combat Reticle / Perfect Hit
- One-Hit Kill

Banked GameShark codes are also supported, including standard `01xx` and WRAM-banked `9Bxx` codes.

### Savestates and Native Saves
The Vita port supports:

- Original battery-backed game saves
- RTC persistence
- 10 savestate slots
- Quick save/load shortcuts

Persistent data is stored under:

```text
ux0:data/regaiden/
```

This includes `config.ini`, runtime preferences, `.sav`, `.rtc`, savestates, snapshots, and user-supplied asset packs.

## Installation

1. Install `ResidentEvilGaiden.vpk` on your PS Vita.
2. Create `ux0:data/regaiden/` if it does not already exist. The application will also create its writable directories automatically when possible.
3. Copy a legally acquired **Resident Evil Gaiden (USA)** Game Boy Color ROM to one of these paths:

```text
ux0:data/regaiden/Resident Evil Gaiden (USA).gbc
ux0:data/regaiden/rom.gbc
```

The ROM is validated before use.

Expected ROM:

- **Title:** Resident Evil Gaiden (USA)
- **Size:** `2,097,152 bytes`
- **SHA256:** `9a97678cbd8da02c8763e977674e17f460c06ea8b73bad35c52fe6817f506d44`

## Controls

| Action | PS Vita Control |
| :--- | :--- |
| Movement | D-Pad |
| Game Boy A / Confirm / Action / Shoot | **Cross** |
| Game Boy B / Cancel | **Circle** |
| In-Game Settings Menu | **Square** |
| Quick Save State | **L** |
| Quick Load State | **R** |
| Start / Inventory | **Start** |
| Select / Map | **Select** |
| Unused | **Triangle** |

## Data Layout

The Vita build keeps writable game data in one location:

```text
ux0:data/regaiden/
  config.ini
  runtime_prefs.ini
  Resident_Evil_Gaiden__USA_.sav
  Resident_Evil_Gaiden__USA_.rtc
  Resident_Evil_Gaiden__USA_.state1
  ...
  hd_pack/
  music_pack/
  snapshots/
```

Exact save filenames may depend on the active game storage ID, but all persistent Vita data is rooted under `ux0:data/regaiden/`.

## Building for PS Vita

### Requirements

- [VitaSDK](https://vitasdk.org/)
- CMake
- Ninja
- PowerShell for the provided build script

Make sure the `VITASDK` environment variable points to your VitaSDK installation.

From the repository root:

```powershell
.\build-vita.ps1 -BuildType Release -Jobs 12
```

The generated package is:

```text
build-vita/ResidentEvilGaiden.vpk
```

The VPK builder also packages the LiveArea resources from `sce_sys/`.

## ROM and Legal Notice

This repository does **not** include the commercial Resident Evil Gaiden ROM or proprietary game assets. You must provide your own legally acquired dump matching the hash listed above.

*Resident Evil* and *Resident Evil Gaiden* are trademarks of their respective owners. This project is an independent open-source recompilation/port and is not affiliated with or endorsed by Capcom.

## Credits

- **Sergio Manzur (sergiomanzur)** - author of the original PC version and the original [`regaiden-recomp`](https://github.com/sergiomanzur/regaiden-recomp) project.
- **Standard-Republic** - PS Vita LiveArea assets.
- **VitaSDK contributors** - PS Vita toolchain and homebrew SDK.
- **SDL2**, **Dear ImGui**, and **stb** contributors - libraries used by the port.

## License

- Project source code: [MIT License](LICENSE)
- Dear ImGui: MIT License
- stb: Public Domain / MIT where applicable
- SDL2: zlib License
