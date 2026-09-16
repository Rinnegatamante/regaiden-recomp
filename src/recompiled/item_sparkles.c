#include "item_sparkles.h"
#include "config_ini.h"
#include "game_state.h"
#include "gbrt_data_mod.h"
#include "ppu.h"
#include "widescreen_ppu.h"
#include <string.h>

#define ROOM_HEADER_BANK 0x50u
#define ENTITY_ACTIVE 0x01u
#define ENTITY_DYNAMIC 0x04u
#define ENTITY_INTERACTABLE 0x40u
#define OBJECT_UNAVAILABLE 0x06u
#define OBJECT_INSTANTIATED 0x08u
#define PICKUP_INTERACTION 0x04u
#define SPARKLE_RADIUS (ITEM_SPARKLE_SIZE / 2)

static uint16_t read_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static bool read_rom(const GBContext* ctx, unsigned bank, unsigned address,
                     uint8_t* out, size_t size) {
    if (address < 0x4000u || address >= 0x8000u || size > 0x8000u - address) return false;
    const size_t offset = (size_t)bank * 0x4000u + address - 0x4000u;
    if (offset > ctx->rom_size || size > ctx->rom_size - offset) return false;
    return gbrt_data_mod_copy_rom(ctx, offset, out, size, false);
}

static bool read_rom_u16(const GBContext* ctx, unsigned bank, unsigned address,
                         uint16_t* out) {
    uint8_t bytes[2];
    if (!read_rom(ctx, bank, address, bytes, sizeof(bytes))) return false;
    *out = read_u16(bytes);
    return true;
}

// 00:22CA dispatches the interaction callback at entity.function_table + 12.
static unsigned pickup_kind(const GBContext* ctx, unsigned bank, unsigned table) {
    uint16_t callback;
    if (bank != 0x53u && bank != 0x0Cu) return 0;
    if (!read_rom_u16(ctx, bank, table + 12u, &callback)) return 0;
    if (bank == 0x53u && callback == 0x4068u) return 1;
    if (bank == 0x0Cu && callback == 0x5F3Du) return 2;
    return 0;
}

// Verify the cached grid against 00:063F / 00:09FE, rejecting half-loaded rooms.
static bool room_grid(const GBContext* ctx, uint16_t* grid, uint8_t* bank) {
    uint16_t header, record;
    uint8_t bytes[3];
    if (!read_rom_u16(ctx, ROOM_HEADER_BANK, 0x4000u + ctx->wram[0x131] * 2u, &header) ||
        !read_rom_u16(ctx, ROOM_HEADER_BANK, (unsigned)header + 8u, &record) ||
        !read_rom(ctx, ROOM_HEADER_BANK, record, bytes, sizeof(bytes))) return false;
    *grid = read_u16(bytes);
    *bank = bytes[2];
    return *grid >= 0x4000u && *grid <= 0x7F80u &&
           *grid == read_u16(ctx->wram + 0x13B) && *bank == ctx->wram[0x13D];
}

static uint8_t palette_opacity(const GBPPU* ppu) {
    int minimum[3] = {31, 31, 31};
    int maximum[3] = {0, 0, 0};
    for (unsigned i = 0; i < 64u; i += 2u) {
        const uint16_t color = read_u16(ppu->bg_palette_ram + i);
        for (unsigned channel = 0; channel < 3u; ++channel) {
            const int value = (color >> (channel * 5u)) & 31;
            if (value < minimum[channel]) minimum[channel] = value;
            if (value > maximum[channel]) maximum[channel] = value;
        }
    }
    int range = 0;
    for (unsigned channel = 0; channel < 3u; ++channel) {
        const int span = maximum[channel] - minimum[channel];
        if (span > range) range = span;
    }
    // Uniform fade-to-black/white palettes must not leave floating highlights.
    return (uint8_t)(range >= 16 ? 255 : range * 16);
}

static void add_sparkle(ItemSparkleFrame* frame, int world_x, int world_y,
                        int camera_x, int camera_y, int origin_x, unsigned id,
                        uint8_t item, uint64_t tick, uint8_t room) {
    const int x = world_x - camera_x + origin_x;
    const int y = world_y - camera_y;
    if (x < frame->clip_left || x >= frame->clip_right ||
        y < frame->clip_top || y >= frame->clip_bottom ||
        frame->count >= ITEM_SPARKLE_MAX_ITEMS) return;
    if (y >= frame->window_y && (x < frame->lower_left || x >= frame->lower_right)) return;
    static const uint8_t pulse[16] = {0, 1, 2, 4, 6, 7, 6, 4, 2, 1, 0, 0, 0, 0, 0, 0};
    const unsigned phase = (unsigned)((tick % 96u + id * 29u + room * 11u) % 96u) / 6u;
    ItemSparkle* sparkle = &frame->items[frame->count++];
    sparkle->x = (int16_t)x;
    sparkle->y = (int16_t)y;
    sparkle->object_id = (uint16_t)id;
    sparkle->item_id = item;
    sparkle->level = pulse[phase];
}

void item_sparkles_build_frame(const GBContext* ctx, int width, int height,
                               ItemSparkleFrame* frame) {
    if (!frame) return;
    memset(frame, 0, sizeof(*frame));
    if (!g_app_config.item_sparkles || !ctx || !ctx->wram || !ctx->rom || !ctx->ppu || !ctx->io ||
        (width != GB_NATIVE_WIDTH && width != GB_WIDESCREEN_WIDTH) || height != GB_NATIVE_HEIGHT ||
        ctx->wram[0x13A] != 1u || gb_state_is_ui_screen(ctx) ||
        (ctx->io[0x40] & 0x83u) != 0x83u) return;

    const unsigned player_page = ctx->wram[0x382];
    if (player_page < 0xD0u || player_page >= 0xE0u) return;
    const uint8_t* player = ctx->wram + 0x1000u + (player_page - 0xD0u) * 0x100u;
    if (!(player[0] & ENTITY_ACTIVE) || player[2] != 0x0Cu || read_u16(player + 5) != 0x4005u) return;

    uint16_t grid;
    uint8_t grid_bank;
    if (!room_grid(ctx, &grid, &grid_bank)) return;
    frame->opacity = palette_opacity((const GBPPU*)ctx->ppu);
    if (!frame->opacity) return;

    const int camera_x = read_u16(ctx->wram + 0x1C5);
    const int camera_y = read_u16(ctx->wram + 0x1C7);
    const int room_width = (int)read_u16(ctx->wram + 0x1DF) + 1;
    const int band = width == GB_WIDESCREEN_WIDTH ? GB_WIDESCREEN_SIDE_BAND_WIDTH : 0;
    const int origin_x = (width - GB_NATIVE_WIDTH) / 2 + band;
    int left = band;
    int right = width - band;
    if (origin_x - camera_x > left) left = origin_x - camera_x;
    if (origin_x - camera_x + room_width < right) right = origin_x - camera_x + room_width;
    const int bottom = height;
    int window_y = height;
    int lower_left = left;
    int lower_right = right;
    if ((ctx->io[0x40] & 0x20u) && ctx->io[0x4A] < height) {
        window_y = ctx->io[0x4A];
        int uncovered_width = (int)ctx->io[0x4B] - 7;
        if (uncovered_width < 0) uncovered_width = 0;
        if (uncovered_width > GB_NATIVE_WIDTH) uncovered_width = GB_NATIVE_WIDTH;
        if (lower_left < origin_x) lower_left = origin_x;
        if (lower_right > origin_x + uncovered_width) lower_right = origin_x + uncovered_width;
    }
    if (left >= right || (window_y <= 16 && lower_left >= lower_right)) return;
    frame->width = (uint16_t)width;
    frame->height = (uint16_t)height;
    frame->clip_left = (int16_t)left;
    frame->clip_top = 0;
    frame->clip_right = (int16_t)right;
    frame->clip_bottom = (int16_t)bottom;
    frame->window_y = (int16_t)window_y;
    frame->lower_left = (int16_t)lower_left;
    frame->lower_right = (int16_t)lower_right;

    bool seen[256] = {false};
    const uint8_t room = ctx->wram[0x131];
    for (unsigned slot = 0; slot < 16u; ++slot) {
        const uint8_t* entity = ctx->wram + 0x1000u + slot * 0x100u;
        if (!(entity[0] & ENTITY_ACTIVE)) continue;
        const bool dynamic = (entity[0] & ENTITY_DYNAMIC) != 0;
        const unsigned id = dynamic ? 256u + slot : entity[7];
        if (!dynamic) seen[id] = true;
        if (!(entity[0] & ENTITY_INTERACTABLE) || entity[0x72] != PICKUP_INTERACTION ||
            (!dynamic && (ctx->wram[0x500u + id] & OBJECT_UNAVAILABLE))) continue;
        const unsigned kind = pickup_kind(ctx, entity[2], read_u16(entity + 5));
        if (!kind) continue;
        const uint8_t item = entity[kind == 1u ? 0x89 : 0x8C];
        if (kind == 2u && item == 0u) continue;
        // The same 12.4 world anchor is used by the original entity/OAM renderer.
        add_sparkle(frame, read_u16(entity + 0x2E) >> 4, read_u16(entity + 0x30) >> 4,
                    camera_x, camera_y, origin_x, id, item, ctx->completed_frames, room);
    }

    // Scan only visible 256px grid cells, including the wider left/right margins.
    int world_left = camera_x + left - origin_x;
    int world_right = camera_x + right - origin_x - 1;
    if (world_left < 0) world_left = 0;
    const int last_row = (camera_y + bottom - 1) / 256;
    for (int row = camera_y / 256; row <= last_row; ++row) {
        for (int col = world_left / 256; col <= world_right / 256; ++col) {
            uint16_t list;
            const unsigned cell = (unsigned)((row & 7) * 8 + (col & 7));
            if (!read_rom_u16(ctx, grid_bank, (unsigned)grid + cell * 2u, &list)) continue;
            for (unsigned n = 0, address = list; n < 256u; ++n, address += 16u) {
                uint8_t entry[16];
                if (!read_rom(ctx, grid_bank, address, entry, sizeof(entry)) || entry[0] == 0xFFu) break;
                const unsigned id = entry[0];
                if (seen[id]) continue;
                // 00:0B2F uses bits 1/2/3; bit 1 is set only on successful pickup.
                if (ctx->wram[0x500u + id] & (OBJECT_UNAVAILABLE | OBJECT_INSTANTIATED)) continue;
                const unsigned story = ctx->wram[0x2E1];
                if (entry[12] && (story < entry[11] || story >= entry[12])) continue;
                if (entry[15] != 0x53u || pickup_kind(ctx, entry[15], (unsigned)read_u16(entry + 13) + 5u) != 1u) continue;
                const unsigned previous_count = frame->count;
                add_sparkle(frame, read_u16(entry + 3) >> 4, read_u16(entry + 5) >> 4,
                            camera_x, camera_y, origin_x, id, entry[7], ctx->completed_frames, room);
                if (frame->count != previous_count) seen[id] = true;
            }
        }
    }
}

bool item_sparkles_get_quad(const ItemSparkleFrame* frame, unsigned index, unsigned part,
                            ItemSparkleQuad* quad) {
    if (!frame || !quad || part > 1u || index >= frame->count || index >= ITEM_SPARKLE_MAX_ITEMS ||
        (frame->width != GB_NATIVE_WIDTH && frame->width != GB_WIDESCREEN_WIDTH) ||
        frame->height != GB_NATIVE_HEIGHT ||
        frame->items[index].level >= ITEM_SPARKLE_LEVELS) return false;
    const ItemSparkle* sparkle = &frame->items[index];
    int x = sparkle->x - SPARKLE_RADIUS;
    int y = sparkle->y - SPARKLE_RADIUS;
    int right = x + ITEM_SPARKLE_SIZE;
    int bottom = y + ITEM_SPARKLE_SIZE;
    if (x < frame->clip_left) x = frame->clip_left;
    if (y < frame->clip_top) y = frame->clip_top;
    if (right > frame->clip_right) right = frame->clip_right;
    if (bottom > frame->clip_bottom) bottom = frame->clip_bottom;
    // Split at the window edge, keeping uncovered world pixels beside partial HUDs.
    if (part == 0u) {
        if (bottom > frame->window_y) bottom = frame->window_y;
    } else {
        if (y < frame->window_y) y = frame->window_y;
        if (x < frame->lower_left) x = frame->lower_left;
        if (right > frame->lower_right) right = frame->lower_right;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > frame->width) right = frame->width;
    if (bottom > frame->height) bottom = frame->height;
    if (x >= right || y >= bottom) return false;
    quad->src_x = sparkle->level * ITEM_SPARKLE_SIZE + x - sparkle->x + SPARKLE_RADIUS;
    quad->src_y = y - sparkle->y + SPARKLE_RADIUS;
    quad->x = x;
    quad->y = y;
    quad->width = right - x;
    quad->height = bottom - y;
    return true;
}

static uint32_t sparkle_pixel(unsigned level, int x, int y) {
    const int dx = x > SPARKLE_RADIUS ? x - SPARKLE_RADIUS : SPARKLE_RADIUS - x;
    const int dy = y > SPARKLE_RADIUS ? y - SPARKLE_RADIUS : SPARKLE_RADIUS - y;
    const int radius = 1 + (int)level / 3;
    const unsigned strength = 96u + level * 22u;
    if (!dx && !dy) return ((strength + 5u) << 24) | 0xFFFFECu;
    if ((!dx || !dy) && dx + dy <= radius) {
        const unsigned alpha = strength * (unsigned)(radius + 1 - dx - dy) / (unsigned)(radius + 1);
        return (alpha << 24) | 0xFFE294u;
    }
    if (dx == 1 && dy == 1 && level >= 5u) return ((strength / 4u) << 24) | 0xFFC85Au;
    return 0;
}

void item_sparkles_build_atlas(uint32_t* pixels) {
    if (!pixels) return;
    for (unsigned level = 0; level < ITEM_SPARKLE_LEVELS; ++level) {
        for (int y = 0; y < ITEM_SPARKLE_SIZE; ++y) {
            for (int x = 0; x < ITEM_SPARKLE_SIZE; ++x) {
                pixels[y * ITEM_SPARKLE_ATLAS_WIDTH + level * ITEM_SPARKLE_SIZE + x] = sparkle_pixel(level, x, y);
            }
        }
    }
}

void item_sparkles_blit(const ItemSparkleFrame* frame, uint32_t* pixels) {
    if (!frame || !pixels || !frame->opacity ||
        (frame->width != GB_NATIVE_WIDTH && frame->width != GB_WIDESCREEN_WIDTH) ||
        frame->height != GB_NATIVE_HEIGHT) return;
    const unsigned count = frame->count < ITEM_SPARKLE_MAX_ITEMS ? frame->count : ITEM_SPARKLE_MAX_ITEMS;
    for (unsigned draw = 0; draw < count * 2u; ++draw) {
        const unsigned i = draw / 2u;
        ItemSparkleQuad quad;
        if (!item_sparkles_get_quad(frame, i, draw & 1u, &quad)) continue;
        for (int y = 0; y < quad.height; ++y) {
            for (int x = 0; x < quad.width; ++x) {
                const uint32_t color = sparkle_pixel(frame->items[i].level,
                    quad.src_x % ITEM_SPARKLE_SIZE + x, quad.src_y + y);
                const unsigned alpha = (color >> 24) * frame->opacity / 255u;
                if (!alpha) continue;
                uint32_t* dst = &pixels[(quad.y + y) * frame->width + quad.x + x];
                uint32_t result = *dst & 0xFF000000u;
                for (unsigned shift = 0; shift < 24u; shift += 8u) {
                    const unsigned value = (((color >> shift) & 255u) * alpha +
                        ((*dst >> shift) & 255u) * (255u - alpha) + 127u) / 255u;
                    result |= value << shift;
                }
                *dst = result;
            }
        }
    }
}
