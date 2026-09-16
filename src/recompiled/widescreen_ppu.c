#include "widescreen_ppu.h"
#include "ppu.h"
#include "game_state.h"
#include <string.h>

uint32_t g_wide_framebuffer[GB_MAX_FRAMEBUFFER_SIZE];

typedef struct WideOamX {
    int16_t x;
    bool valid;
} WideOamX;

static WideOamX s_wide_oam_x[40];
static uint8_t s_shadow_oam_before[160];
static int16_t s_capture_entity_x;
static bool s_capture_active;

static uint16_t read_guest_u16(GBContext* ctx, uint16_t addr) {
    return (uint16_t)(gb_read8(ctx, addr) | ((uint16_t)gb_read8(ctx, (uint16_t)(addr + 1u)) << 8));
}

static int camera_x_for_presented_frame(GBContext* ctx) {
    const uint16_t world_camera_x = read_guest_u16(ctx, 0xC1C5u);
    const uint8_t scx = ctx->io[0x43];
    int display_camera_x = (int)((world_camera_x & 0xFF00u) | scx);
    const int delta = display_camera_x - (int)world_camera_x;

    if (delta > 128) display_camera_x -= 256;
    else if (delta < -128) display_camera_x += 256;

    return display_camera_x;
}

void widescreen_entity_frame_begin(GBContext* ctx) {
    (void)ctx;
    memset(s_wide_oam_x, 0, sizeof(s_wide_oam_x));
    s_capture_active = false;
}

bool widescreen_entity_should_extend_x(int16_t relative_x) {
    const int target_w = widescreen_get_target_width();
    if (target_w <= GB_NATIVE_WIDTH) return false;

    // Gaiden natively accepts about [-32,191]; widen only into the side columns plus the same 32px sprite margin.
    if (relative_x >= -32 && relative_x < 192) return false;

    const int extra = (target_w - GB_NATIVE_WIDTH) / 2;
    return relative_x >= -(32 + extra) && relative_x < (192 + extra);
}

void widescreen_entity_capture_begin(GBContext* ctx, uint8_t entity_page) {
    s_capture_active = false;
    if (!ctx || !ctx->wram || widescreen_get_target_width() <= GB_NATIVE_WIDTH ||
        entity_page < 0xD0u || entity_page > 0xDFu) {
        return;
    }

    const uint16_t base = (uint16_t)entity_page << 8;
    const uint16_t world_x = (uint16_t)(read_guest_u16(ctx, (uint16_t)(base + 0x2Eu)) >> 4);
    const int camera_x = camera_x_for_presented_frame(ctx);
    s_capture_entity_x = (int16_t)((int)world_x - camera_x);

    memcpy(s_shadow_oam_before, ctx->wram + 0x700u, sizeof(s_shadow_oam_before));
    s_capture_active = true;
}

void widescreen_entity_capture_end(GBContext* ctx) {
    if (!s_capture_active || !ctx || !ctx->wram) {
        s_capture_active = false;
        return;
    }

    const uint8_t* shadow = ctx->wram + 0x700u;
    for (int i = 0; i < 40; ++i) {
        const size_t off = (size_t)i * 4u;
        if (shadow[off] == s_shadow_oam_before[off] &&
            shadow[off + 1u] == s_shadow_oam_before[off + 1u]) {
            continue;
        }
        if (shadow[off] == 0u || shadow[off + 1u] == 0u) {
            continue;
        }

        int x = (int)shadow[off + 1u] - 8;
        while (x - s_capture_entity_x > 128) x -= 256;
        while (s_capture_entity_x - x > 128) x += 256;
        s_wide_oam_x[i].x = (int16_t)x;
        s_wide_oam_x[i].valid = true;
    }
    s_capture_active = false;
}

/* Window start line at or above which the window is covering the whole screen. */
#define FULLSCREEN_UI_MAX_WY 16

static inline uint32_t rgb555_to_rgba(uint16_t color) {
    uint8_t r = (uint8_t)(((color >> 0) & 0x1F) * 255 / 31);
    uint8_t g = (uint8_t)(((color >> 5) & 0x1F) * 255 / 31);
    uint8_t b = (uint8_t)(((color >> 10) & 0x1F) * 255 / 31);
    return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

int widescreen_get_target_width(void) {
    switch (g_app_config.widescreen_mode) {
        case ASPECT_WIDESCREEN_16_9:
            return GB_WIDESCREEN_WIDTH;
        case ASPECT_NATIVE_10_9:
        default:
            return GB_NATIVE_WIDTH;
    }
}

int widescreen_get_target_height(void) {
    return GB_NATIVE_HEIGHT;
}


/*
 * Copy the dot-accurate 160x144 frame produced by the real PPU into the middle
 * of the widescreen buffer.
 *
 * The wide re-render below works off a single end-of-frame snapshot of VRAM,
 * OAM and the LCD registers, so it cannot reproduce anything the game changes
 * part-way through a frame (HBlank HDMA tile streaming on the item viewer,
 * window and palette raster effects behind dialogue portraits). Those scenes
 * came out striped and flickering. The PPU already rendered them correctly
 * scanline by scanline, so the native viewport is taken verbatim from it and
 * only the newly revealed side columns come from the approximate re-render.
 */
static void blit_native_at(uint32_t* out_fb, int target_w, int dest_x, const uint32_t* native_fb) {
    for (int y = 0; y < GB_NATIVE_HEIGHT; y++) {
        memcpy(&out_fb[(size_t)y * (size_t)target_w + (size_t)dest_x],
               &native_fb[(size_t)y * GB_NATIVE_WIDTH],
               GB_NATIVE_WIDTH * sizeof(uint32_t));
    }
}

static void build_rgba_palettes(const GBPPU* ppu, uint32_t bg[8][4], uint32_t obj[8][4]) {
    for (int pal = 0; pal < 8; ++pal) {
        for (int color = 0; color < 4; ++color) {
            const size_t idx = (size_t)pal * 8u + (size_t)color * 2u;
            const uint16_t bg_color = (uint16_t)(ppu->bg_palette_ram[idx] | ((uint16_t)ppu->bg_palette_ram[idx + 1u] << 8));
            const uint16_t obj_color = (uint16_t)(ppu->obj_palette_ram[idx] | ((uint16_t)ppu->obj_palette_ram[idx + 1u] << 8));
            bg[pal][color] = rgb555_to_rgba(bg_color);
            obj[pal][color] = rgb555_to_rgba(obj_color);
        }
    }
}

void widescreen_render_frame(GBContext* ctx, const uint32_t* native_fb, uint32_t* out_fb,
                             int* out_width, int* out_height) {
    if (!ctx || !out_fb) return;

    int target_w = widescreen_get_target_width();
    int target_h = widescreen_get_target_height();

    if (out_width) *out_width = target_w;
    if (out_height) *out_height = target_h;

    // Prefer the exact frame the platform layer chose to present; fall back to
    // the PPU's live framebuffer only when the caller has none.
    if (!native_fb) {
        native_fb = gb_get_framebuffer(ctx);
    }

    const GBPPU* ppu = (const GBPPU*)ctx->ppu;
    const uint8_t* vram = ctx->vram;

    if (!ppu || !vram || (ctx->io[0x40] & LCDC_LCD_ENABLE) == 0) {
        memset(out_fb, 0, (size_t)target_w * (size_t)target_h * sizeof(uint32_t));
        if (native_fb) {
            const int native_x = target_w == GB_WIDESCREEN_WIDTH
                ? GB_WIDESCREEN_SIDE_BAND_WIDTH + (GB_WIDESCREEN_WIDTH - GB_NATIVE_WIDTH) / 2
                : (target_w - GB_NATIVE_WIDTH) / 2;
            blit_native_at(out_fb, target_w, native_x, native_fb);
        }
        return;
    }

    // Native 10:9 mode
    if (g_app_config.widescreen_mode == ASPECT_NATIVE_10_9 || target_w == GB_NATIVE_WIDTH) {
        if (native_fb) {
            memcpy(out_fb, native_fb, GB_NATIVE_WIDTH * GB_NATIVE_HEIGHT * sizeof(uint32_t));
        }
        return;
    }

    uint8_t lcdc = ctx->io[0x40];
    uint8_t scy = ctx->io[0x42];
    uint8_t wy = ctx->io[0x4A];
    uint8_t wx = ctx->io[0x4B];

    bool bg_enable = (lcdc & LCDC_BG_ENABLE) != 0;
    bool win_enable = (lcdc & LCDC_WINDOW_ENABLE) != 0;
    bool obj_enable = (lcdc & LCDC_OBJ_ENABLE) != 0;
    bool obj_8x16 = (lcdc & LCDC_OBJ_SIZE) != 0;
    bool unsigned_tile_data = (lcdc & LCDC_TILE_DATA) != 0;

    uint16_t bg_tilemap_offset = (lcdc & LCDC_BG_TILEMAP) ? 0x1C00 : 0x1800;
    const int source_native_x = (target_w - GB_NATIVE_WIDTH) / 2;
    const int output_shift = GB_WIDESCREEN_SIDE_BAND_WIDTH;
    const int final_native_x = source_native_x + output_shift;
    const int right_extension_start = source_native_x + GB_NATIVE_WIDTH;
    const int display_camera_x = camera_x_for_presented_frame(ctx);
    const uint16_t room_max_x = read_guest_u16(ctx, 0xC1DFu);
    const int room_width = room_max_x != 0u ? (int)room_max_x + 1 : 0;
    memset(out_fb, 0, (size_t)target_w * (size_t)target_h * sizeof(uint32_t));
    if (native_fb) {
        blit_native_at(out_fb, target_w, final_native_x, native_fb);
    }

    // Extend only trustworthy world VRAM; UI screens leave stale data outside the native 20 tile columns.
    const bool ui_covers_screen = win_enable && (wx <= 166) && (wy <= FULLSCREEN_UI_MAX_WY);

    // Treat UI screens as untrusted because they update only the native 20 tile columns.
    const bool extension_untrusted =
        ui_covers_screen || gb_state_is_ui_screen(ctx) || !bg_enable;

    if (extension_untrusted) {
        return;
    }

    uint32_t bg_rgba[8][4];
    uint32_t obj_rgba[8][4];
    build_rgba_palettes(ppu, bg_rgba, obj_rgba);

    static uint8_t bg_color_idx[GB_WIDESCREEN_CONTENT_WIDTH];
    static uint8_t bg_priority_flags[GB_WIDESCREEN_CONTENT_WIDTH];
    const int span_start[2] = { 0, right_extension_start };
    const int span_end[2] = { source_native_x, GB_WIDESCREEN_CONTENT_WIDTH };

    for (int y = 0; y < GB_NATIVE_HEIGHT; y++) {
        uint32_t* row_dst = &out_fb[y * target_w];
        memset(bg_color_idx, 0, sizeof(bg_color_idx));
        memset(bg_priority_flags, 0, sizeof(bg_priority_flags));
        int bg_y = (y + (int)scy) & 0xFF;
        uint8_t tile_y = (uint8_t)(bg_y / 8);
        uint8_t fine_y = (uint8_t)(bg_y % 8);

        if (win_enable && y >= (int)wy) {
            continue;
        }

        for (int span = 0; span < 2; ++span) {
            int x = span_start[span];
            while (x < span_end[span]) {
                const int absolute_world_x = display_camera_x + (x - source_native_x);
                if (room_width > 0 && (absolute_world_x < 0 || absolute_world_x >= room_width)) {
                    ++x;
                    continue;
                }

                const int world_x = absolute_world_x & 0xFF;
                const uint8_t tile_x = (uint8_t)(world_x >> 3);
                const uint8_t fine_x = (uint8_t)(world_x & 7);
                int run = 8 - (int)fine_x;
                if (run > span_end[span] - x) run = span_end[span] - x;
                if (room_width > 0 && absolute_world_x + run > room_width) {
                    run = room_width - absolute_world_x;
                }
                if (run <= 0) {
                    ++x;
                    continue;
                }

                const uint16_t map_idx = (uint16_t)(bg_tilemap_offset + tile_y * 32u + tile_x);
                const uint8_t tile_idx = vram[map_idx];
                const uint8_t attr = vram[VRAM_SIZE + map_idx];
                const uint8_t pal_idx = attr & 0x07u;
                const uint8_t tile_bank = (attr & 0x08u) ? 1u : 0u;
                const bool flip_x = (attr & 0x20u) != 0u;
                const uint8_t px_y = (attr & 0x40u) ? (uint8_t)(7u - fine_y) : fine_y;
                const uint8_t priority = (attr & 0x80u) ? 1u : 0u;
                const uint16_t tile_offset = unsigned_tile_data
                    ? (uint16_t)(tile_idx * 16u + px_y * 2u)
                    : (uint16_t)(0x1000 + ((int8_t)tile_idx * 16) + px_y * 2u);
                const uint8_t lo = vram[(size_t)tile_bank * VRAM_SIZE + tile_offset];
                const uint8_t hi = vram[(size_t)tile_bank * VRAM_SIZE + tile_offset + 1u];

                for (int i = 0; i < run; ++i) {
                    const uint8_t f_x = (uint8_t)(fine_x + i);
                    const int bit = flip_x ? (int)f_x : 7 - (int)f_x;
                    const uint8_t color_idx = (uint8_t)(((lo >> bit) & 1u) | (((hi >> bit) & 1u) << 1u));
                    const int screen_x = x + i;
                    bg_color_idx[screen_x] = color_idx;
                    bg_priority_flags[screen_x] = priority;
                    row_dst[screen_x + output_shift] = bg_rgba[pal_idx][color_idx];
                }
                x += run;
            }
        }

        if (obj_enable) {
            uint8_t spr_height = obj_8x16 ? 16 : 8;

            for (int i = 0; i < 40; i++) {
                const uint8_t* sprite = ctx->oam + (i * 4);
                int spr_y = (int)sprite[0] - 16;
                int spr_x = (s_wide_oam_x[i].valid
                    ? (int)s_wide_oam_x[i].x
                    : (int)sprite[1] - 8) + source_native_x;
                uint8_t tile_idx = sprite[2];
                uint8_t flags = sprite[3];

                if (y < spr_y || y >= spr_y + spr_height) {
                    continue;
                }

                int line = y - spr_y;
                bool flip_x = (flags & OAM_FLIP_X) != 0;
                bool flip_y = (flags & OAM_FLIP_Y) != 0;
                bool behind_bg = (flags & OAM_PRIORITY) != 0;
                uint8_t pal_idx = flags & OAM_CGB_PALETTE;
                uint8_t tile_bank = (flags & OAM_CGB_BANK) ? 1 : 0;

                if (obj_8x16) {
                    tile_idx &= 0xFE;
                    if (flip_y) line = 15 - line;
                    if (line >= 8) {
                        tile_idx |= 1;
                        line -= 8;
                    }
                } else if (flip_y) {
                    line = 7 - line;
                }

                uint16_t tile_offset = (uint16_t)(tile_idx * 16 + line * 2);
                uint8_t lo = vram[(tile_bank * VRAM_SIZE) + tile_offset];
                uint8_t hi = vram[(tile_bank * VRAM_SIZE) + tile_offset + 1];

                for (int px = 0; px < 8; px++) {
                    int screen_x = spr_x + px;
                    if (screen_x < 0 || screen_x >= GB_WIDESCREEN_CONTENT_WIDTH) {
                        continue;
                    }
                    if (screen_x >= source_native_x && screen_x < right_extension_start) {
                        continue;
                    }

                    int bit = flip_x ? px : (7 - px);
                    uint8_t color_idx = (uint8_t)(((lo >> bit) & 1) | (((hi >> bit) & 1) << 1));

                    if (color_idx == 0) {
                        continue; // Transparent sprite pixel
                    }

                    // Priority handling: if behind BG and BG color != 0, skip
                    if (behind_bg && bg_color_idx[screen_x] != 0) {
                        continue;
                    }
                    if (bg_priority_flags[screen_x] && bg_color_idx[screen_x] != 0) {
                        continue;
                    }

                    row_dst[screen_x + output_shift] = obj_rgba[pal_idx][color_idx];
                }
            }
        }
    }
}
