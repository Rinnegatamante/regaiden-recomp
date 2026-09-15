#include "gaiden_map.h"
#include "gbrt_data_mod.h"
#include "ppu.h"
#include <algorithm>

namespace revoxel {

uint16_t little_u16(const uint8_t* b) {
    return static_cast<uint16_t>(b[0] | (static_cast<unsigned>(b[1]) << 8));
}

uint32_t color555(uint16_t c) {
    const unsigned r = (c & 31u) * 255u / 31u;
    const unsigned g = ((c >> 5) & 31u) * 255u / 31u;
    const unsigned b = ((c >> 10) & 31u) * 255u / 31u;
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

bool GaidenMap::rom_byte(unsigned bank, unsigned address, uint8_t& value) const {
    if (!context || !context->rom || address >= 0x8000u) return false;
    const size_t offset = address < 0x4000u ? address :
        static_cast<size_t>(bank) * 0x4000u + address - 0x4000u;
    if (offset >= context->rom_size) return false;
    value = context->data_mod_state ? gbrt_data_mod_read_rom(context, offset, false) : context->rom[offset];
    return true;
}

const uint8_t* GaidenMap::entity(unsigned page) const {
    if (!context || !context->wram || page < 0xd0u || page > 0xdfu) return nullptr;
    return context->wram + entity_bank * 0x1000 + (page - 0xd0u) * 0x100;
}

bool GaidenMap::open(const GBContext* ctx) {
    context = ctx;
    if (!ctx || !ctx->wram || !ctx->vram || !ctx->ppu || !ctx->rom) return false;
    const uint8_t* w = ctx->wram;
    width = w[0x19f] ? w[0x19f] : 256;
    height = w[0x1a0] ? w[0x1a0] : 256;
    if ((width & (width - 1)) != 0 || width < 8 || height < 1) return false;
    room = w[0x131];
    flags = w[0x19e];
    map_address = little_u16(w + 0x1a1);
    map_bank = w[0x1a3];
    tile_address = little_u16(w + 0x1a4);
    tile_bank = w[0x1a6];
    attribute_address = little_u16(w + 0x1a7);
    collision_address = little_u16(w + 0x13f);
    collision_bank = w[0x141];
    mask_address = little_u16(w + 0x142);
    mask_bank = w[0x144];
    entity_bank = w[0x13a] & 7;
    if (!entity_bank) entity_bank = 1;
    focus_page = w[0x382];
    camera_x = little_u16(w + 0x1c5);
    camera_y = little_u16(w + 0x1c7);
    const uint8_t* focus = entity(focus_page);
    layer_mask = focus ? focus[0x6e] : 0xff;
    if (camera_x >= width * 16 || camera_y >= height * 16) return false;
    if ((tile_address & 255u) || (attribute_address & 255u)) return false;
    if (tile_address < 0x4000 || attribute_address < 0x4000 ||
        collision_address < 0x4000 || mask_address < 0x4000) return false;
    if ((flags & 8) && width * height > 0x1000) return false;
    if ((!(flags & 8) || (flags & 4)) && (map_address < 0x4000 ||
        map_address + static_cast<unsigned>(width * height) > 0x8000)) return false;
    uint8_t probe;
    return rom_byte(tile_bank, tile_address, probe) &&
        rom_byte(tile_bank, attribute_address, probe) &&
        rom_byte(collision_bank, collision_address + 0x800, probe) &&
        rom_byte(mask_bank, mask_address + 0x7ff, probe);
}

bool GaidenMap::metatile(int x, int y, uint16_t& value, bool* visible) const {
    if (!context || x < 0 || y < 0 || x >= width || y >= height) return false;
    const unsigned index = static_cast<unsigned>(y * width + x);
    uint8_t low = 0, high = 0;
    if (flags & 8) {
        if (index >= 0x1000) return false;
        low = context->wram[0x5000 + index];
        high = context->wram[0x6000 + index];
    } else if (!rom_byte(map_bank, map_address + index, low) ||
               !rom_byte(map_bank + 1, map_address + index, high)) {
        return false;
    }
    value = static_cast<uint16_t>(low | (static_cast<unsigned>(high) << 8));
    if (visible) {
        *visible = true;
        if (flags & 4) {
            uint8_t layer;
            if (!rom_byte(map_bank + 2, map_address + index, layer)) return false;
            *visible = (layer & layer_mask) != 0;
        }
    }
    return true;
}

bool GaidenMap::cell(int x, int y, MapCell& out) const {
    out = {};
    if (x < 0 || y < 0 || !metatile(x / 2, y / 2, out.metatile, &out.visible)) return false;
    const unsigned q = (x & 1) | ((y & 1) << 1);
    const unsigned high = out.metatile >> 8;
    const unsigned rotated = ((high << 2) | (high >> 6)) & 255u;
    const unsigned offset = (out.metatile & 255u) + ((rotated + q) << 8);
    if (!rom_byte(tile_bank, (tile_address + offset) & 0xffffu, out.tile) ||
        !rom_byte(tile_bank, (attribute_address + offset) & 0xffffu, out.attributes)) return false;

    // 00:1732 selects four 0x200-byte collision planes, NOT four 0x100-byte graphics planes.
    const unsigned address = collision_address + out.metatile + q * 0x200;
    uint8_t mask, type;
    if (!rom_byte(collision_bank, address, mask) ||
        !rom_byte(collision_bank, address + 0x800, type)) return false;
    out.collision_type = type & 31;
    for (unsigned row = 0; row < 8; ++row) {
        if (!rom_byte(mask_bank, mask_address + mask * 8u + row, out.collision[row])) return false;
    }
    return true;
}

bool GaidenMap::blocked(int x, int y, bool& value, uint8_t* type) const {
    MapCell c;
    if (x < 0 || y < 0 || !cell(x / 8, y / 8, c)) return false;
    if (type) *type = c.collision_type;
    value = c.collision_type < 4 && (c.collision[y & 7] & (0x80u >> (x & 7))) != 0;
    return true;
}

uint32_t GaidenMap::palette_pixel(bool object, uint8_t attributes, unsigned index) const {
    const auto* ppu = static_cast<const GBPPU*>(context->ppu);
    const uint8_t* palette = object ? ppu->obj_palette_ram : ppu->bg_palette_ram;
    const unsigned offset = (attributes & 7u) * 8u + index * 2u;
    return color555(little_u16(palette + offset));
}

uint32_t GaidenMap::background_pixel(const MapCell& c, int x, int y) const {
    if (!c.visible || !context) return 0;
    x &= 7; y &= 7;
    if (c.attributes & 0x20) x = 7 - x;
    if (c.attributes & 0x40) y = 7 - y;
    const auto* ppu = static_cast<const GBPPU*>(context->ppu);
    const int tile = (ppu->lcdc & 0x10) ? c.tile * 16 : 0x1000 + static_cast<int8_t>(c.tile) * 16;
    const int address = ((c.attributes & 8) ? 0x2000 : 0) + tile + y * 2;
    const unsigned low = context->vram[address];
    const unsigned high = context->vram[address + 1];
    const unsigned bit = 7 - x;
    return palette_pixel(false, c.attributes, ((low >> bit) & 1u) | (((high >> bit) & 1u) << 1));
}

uint32_t GaidenMap::object_pixel(uint8_t tile, uint8_t attributes, int x, int y, int height) const {
    if (!context || x < 0 || x >= 8 || y < 0 || y >= height || (height != 8 && height != 16)) return 0;
    if (attributes & 0x20) x = 7 - x;
    if (attributes & 0x40) y = height - 1 - y;
    if (height == 16) tile = static_cast<uint8_t>((tile & 0xfe) + y / 8);
    const int address = ((attributes & 8) ? 0x2000 : 0) + tile * 16 + (y & 7) * 2;
    const unsigned bit = 7 - x;
    const unsigned index = ((context->vram[address] >> bit) & 1u) |
        (((context->vram[address + 1] >> bit) & 1u) << 1);
    return index ? palette_pixel(true, attributes, index) : 0;
}

}
