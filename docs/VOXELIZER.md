# Voxelized exploration

## Enable and configure

Open the settings overlay (`F10` on PC, the settings icon on Vita), select
**Voxelizer 3D**, and enable **Enable voxelized exploration**. Disabling it restores
the existing native/widescreen renderer. The feature is disabled by default.

Vita starts with **Performance (320x180)**; **Detailed (480x272)** is also available.
These are internal rendering resolutions, not measured frame-rate guarantees.
At shallow or strongly rotated angles, zoom is automatically increased only as
needed to keep the finite world patch outside the view. Effective zoom is shown
in the status line; it does not fluctuate as the player crosses patch boundaries.
The tab also controls camera elevation, rotation, zoom, default wall/object
heights, shadows, and foreground-wall cutaways. Settings persist in `config.ini`:

```ini
[Voxelizer]
enabled=1
quality=0
pitch=55
yaw=-12
zoom=110
wall_height=28
prop_height=10
shadows=1
cutaway=1
```

The original simulation, movement, collision handling, combat rules and save-state
layout are unchanged. The 3D renderer owns its lighting. The original 2D
flashlight, screen-space post-processing, HD overlays and widescreen side masks
are not applied to the 3D image. They remain available on the normal 2D path.

## Geometry and synchronization

This is a perspective mesh renderer with a per-pixel depth buffer, not a tilted
copy of the completed Game Boy framebuffer. It reconstructs a bounded 384x384
world-pixel neighborhood, reads the room's metatiles and collision masks, and
meshes raised surfaces with exposed sides and tops. Nearest-neighbor sampling
keeps the original pixel art. Wall strips are folded into facades, props preserve
their top artwork, and floor reconstruction fills the ground exposed by cutaways.
Contact shadows, directional shadows and directional face shading convey depth.

Collision footprints come from the game's movement code. Heights do not: the
Game Boy data does not automatically provide an authored 3D scene. Connected
collision components supply conservative wall/prop height estimates. Explicit
profiles are the mechanism for correcting special objects, false visual walls,
stairs, roofs and room-specific heights. Neither color darkness nor the CGB
background-priority bit classifies a tile as a wall.

Metasprites are associated with their actual entity while Gaiden writes shadow
OAM. Ownership is latched when OAM DMA completes, rather than inferred from
proximity or changes to sprite bytes. The renderer assembles each character and
extrudes its opaque pixels into a two-pixel-thick, upright, camera-facing sprite.
This preserves separate overlapping characters and stationary sprites. Logical
feet and animation offsets are retained; explicit walkable platforms can lift
actors without placing them on solid obstacles.

The PPU hooks capture complete displayed frames. Captured camera coordinates
are reconciled with latched SCX/SCY, which Gaiden sets in its VBlank handler.
Additional host presentations and paused camera adjustments reuse a private
completed-frame snapshot instead of reading partially advanced game state.
Screen-space sprites and the PPU window remain a 2D overlay. The renderer falls
back to native 2D when the current screen is not a valid exploration scene.

## Verified game-data paths

Addresses refer to the matching USA recompilation and its banked address space.
The implementation reads physical host memory without changing guest banks.

| Data | Guest location / code |
| --- | --- |
| Room ID | `C131` |
| Entity WRAM bank | `C13A`; entity pages `D0` through `DF` |
| Collision lookup pointer/bank | `C13F..C141` |
| Collision mask pointer/bank | `C142..C144`; initialized to `35:7000` |
| Map flags, width, height | `C19E..C1A0` |
| Metatile map pointer/low bank | `C1A1..C1A3`; high IDs in the next bank |
| Tile-number table pointer/bank | `C1A4..C1A6` |
| Attribute-table pointer | `C1A7..C1A8`; game reads it in the tile-table bank |
| Desired camera X/Y | `C1C5..C1C8` |
| Player / focus entity page | `C380` / `C382` |
| Shadow OAM | `C700..C79F` |
| Collision queries | `00:1732`, `00:17AF`; movement type check at `00:1715` |
| SCX/SCY latch and OAM DMA | VBlank handler `00:2B8A..00:2BAC` |

For a world pixel `(x,y)`, the 16x16 metatile ID and its 8x8 quadrant select:

```text
quadrant = ((x >> 3) & 1) + 2 * ((y >> 3) & 1)
lookup = collision_base + metatile_id + quadrant * 0x200
mask_id = ROM[collision_bank, lookup]
type = ROM[collision_bank, lookup + 0x800] & 31
row = ROM[mask_bank, mask_base + mask_id * 8 + (y & 7)]
blocked = type < 4 AND (row & (0x80 >> (x & 7))) != 0
```

The collision quadrant stride is **0x200**, not the **0x100** used by the graphics
planes. Types 4 and above are not blindly raised as movement-blocking walls.
RAM-backed map planes are read from physical WRAM banks 5 and 6. Optional map
visibility layers use the game's focus-entity visibility mask.

## Explicit shape profiles

Place `voxel_profiles.txt` beside the active `config.ini`. On Vita this is
`ux0:/data/regaiden/voxel_profiles.txt`. The example file is documentation only:
its sample IDs and coordinates are not verified Gaiden room presets.

Each non-comment line has one of these forms:

```text
tile ROOM_OR_STAR METATILE SHAPE HEIGHT
tile_fill ROOM_OR_STAR METATILE SHAPE HEIGHT
rect ROOM_OR_STAR X Y WIDTH DEPTH SHAPE HEIGHT
```

`ROOM_OR_STAR` is a room ID or `*` for all rooms. Numbers accept decimal or `0x`
hexadecimal; avoid leading zeroes in decimal numbers. Coordinates, dimensions
and heights are in original game pixels. Comments start with `#` or `;`.
Supported shape names are `auto`, `flat`, `wall`, `prop`, `rail`, `stairs_n`,
`stairs_s`, `roof_x`, `roof_y` and `platform`.

A `tile` rule changes heights only on that metatile's existing solid footprint;
it does not fill its walkable pixels. `tile_fill` explicitly fills or clears the
entire visible 16x16 metatile, including pixels that are not movement-blocking.
It follows the actual metatile ID: changing that ID removes or replaces its
authored geometry. This can describe decorative objects or special-event tiles
without pretending their collision type alone specifies their appearance.
A `rect` rule explicitly fills or clears geometry throughout a visible rectangle.
Both forms are deliberate authoring operations, not automatic collision guesses.
They change only the rendered geometry, never game collision or movement.
Later matching rules take precedence.

`wall` and `rail` use facade/trim mapping and foreground cutaways. `prop` keeps
top artwork. `auto` sets the given height but retains the automatically selected
wall/prop material style. `flat` clears height. `stairs_n` descends toward
increasing Y; `stairs_s` ascends toward increasing Y. `roof_x` and `roof_y`
produce symmetric stepped ridges across the named axis. `platform` is a level
raised walking surface. Only stairs and platforms lift actors, and only where
the original game allows walking; authored walls, roofs and props do not place
characters on their tops. Heights are limited to 64 pixels. Current geometry is
a height field; it does not represent arbitrary caves, stacked floors, or
overhangs with empty space beneath them.

**Reload shape profiles** applies changes without restarting. Loading is
transactional: invalid input reports a line number and preserves previous rules.
Files are bounded to 256 KiB and 1,024 rules.

## Diagnostics and native-2D fallback

The tab displays the active room, actors, face/triangle counts, composition time,
mesh rebuild count and agreement between the original frame and decoded map.
This image agreement is a safety check for non-world screens, not an elevation
classifier. A fallback message is intentional and should not be hidden by
forcing arbitrary screens through the world renderer.

**Export current 3D scene diagnostics** writes beside `config.ini`:

- `voxel_room_XX_view.ppm`: the actual composed 3D view, including native UI.
- `voxel_room_XX_map.ppm`: decoded source neighborhood.
- `voxel_room_XX_cells.csv`: room/world coordinates, metatile, graphics attributes,
  collision type, eight collision-mask rows and visibility for each 8x8 cell.

Invalid map addresses, missing matching metasprite DMA data, non-exploration
screens, allocation failures, and scenes beyond the 24,000-face budget retain
the native 2D path rather than rendering unchecked geometry. Disabling the
feature releases its host allocations. The old Vita asynchronous 2D presentation
path is drained and bypassed while the voxelizer is enabled, avoiding reuse of
its smaller frame buffers. SDL streaming uploads respect the actual row pitch.

## Tests

The standalone tests require CMake and a C++20/C11 compiler, but no commercial
ROM, SDL window, emulator or Vita device:

```powershell
cmake -S tests/voxel -B testbuild/voxel-tests -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build testbuild/voxel-tests
ctest --test-dir testbuild/voxel-tests --output-on-failure
.\testbuild\voxel-tests\voxel_tests.exe testbuild/voxel-output
```

They cover collision decoding, physical bank selection, CGB palettes/flips,
signed tile addressing, 8x16 sprites, transparent depth, clipping, profile
validation, greedy geometry, allocation/face bounds, cache recovery, unmodified
guest memory, metasprite ownership, OAM DMA latching, latched-camera consistency,
complete-frame reuse, native window composition, room changes and resets.
The executable also reports a **desktop** fixture benchmark and can write PPM
reference images. Large check counts include per-pixel checks and output writes;
they are not a count of independent test cases.

## Validation boundary

The included synthetic fixture uses original test art and the game's decoded
memory layout. It demonstrates geometry, perspective, character thickness,
shadows, stairs/roofs and native-UI composition. It is **not a screenshot of
Resident Evil Gaiden**, and its timing is **not a Vita frame-rate measurement**.

This checkout contained no matching ROM or save during implementation. Real
rooms still need visual validation and, where appropriate, authored profiles;
collision topology alone cannot distinguish every bed, cabinet, wall, railing
or decorative element. Native sprite-generation/culling limits also apply: this
renderer does not invent actors that the game has not submitted. Real combat,
dialogue transitions, unusual map layers and Vita performance must be tested
with the matching game and hardware before claiming a finished all-room look.

For actual-game testing, use the existing ROM workflow documented in
[PSVITA.md](PSVITA.md). Capture the 3D view plus cell diagnostics from representative
corridors, cabins, stairs, deck railings, doorways and overlapping characters.
Correct geometry in profiles only after matching it to the game's actual data.

## References

The visual and architectural reference was
[DramaticShapeVoxelMod](https://github.com/scottcandy34/DramaticShapeVoxelMod-latest).
Its game-specific shape definitions are important: reconstructing meaningful 3D
requires more than scaling a background tile. This implementation is original
C/C++ and does not import that mod's code or assets.

GBC layout and attribute semantics were cross-checked against the primary
[Pan Docs tile data](https://gbdev.io/pandocs/Tile_Data.html),
[tile maps](https://gbdev.io/pandocs/Tile_Maps.html) and
[OAM](https://gbdev.io/pandocs/OAM.html) documentation. Gaiden-specific addresses
above come from the checked-in recompiled instruction bodies.
