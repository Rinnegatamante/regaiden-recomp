#include "lighting.h"
#include "game_state.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Opt-in enhancement: off until the player enables it (see config_set_defaults).
LightingConfig g_lighting_config = {
    .enabled = false,
    .intensity = 85,
    .ambient_darkness = 25,
    .flicker_enabled = true,
    .cone_angle_deg = 65,
    .cone_distance = 140
};

/* Window start line at or above which the window is covering the whole screen. */
#define FULLSCREEN_UI_MAX_WY 16

static PlayerFacingDir s_player_dir = DIR_DOWN;
static uint32_t s_frame_counter = 0;

// Precompute player-relative light falloff once; a 501x501 LUT covers the 250px maximum beam reach.
#define LUT_MAX_W 336
#define LUT_MAX_H 144
#define LIGHT_LUT_RADIUS 250
#define LIGHT_LUT_SIZE (LIGHT_LUT_RADIUS * 2 + 1)
static uint8_t s_light_lut[LIGHT_LUT_SIZE][LIGHT_LUT_SIZE];
static int s_cached_cone_angle = -1;
static int s_cached_cone_dist = -1;

static float normalize_angle(float angle) {
    while (angle > (float)M_PI) angle -= (float)(2.0 * M_PI);
    while (angle < -(float)M_PI) angle += (float)(2.0 * M_PI);
    return angle;
}

static void rebuild_light_luts(void) {
    s_cached_cone_angle = g_lighting_config.cone_angle_deg;
    s_cached_cone_dist = g_lighting_config.cone_distance;

    float half_cone = (float)(g_lighting_config.cone_angle_deg * 0.5 * M_PI / 180.0);
    float max_dist = (float)g_lighting_config.cone_distance;
    if (max_dist > (float)LIGHT_LUT_RADIUS) max_dist = (float)LIGHT_LUT_RADIUS;
    if (max_dist < 1.0f) max_dist = 1.0f;
    float inner_radius = 16.0f;

    for (int y = 0; y < LIGHT_LUT_SIZE; ++y) {
        const float dy = (float)(y - LIGHT_LUT_RADIUS);
        for (int x = 0; x < LIGHT_LUT_SIZE; ++x) {
            const float dx = (float)(x - LIGHT_LUT_RADIUS);
            const float dist = sqrtf(dx * dx + dy * dy);

            float light = 0.0f;
            if (dist < inner_radius) {
                light = 1.0f - (dist / inner_radius) * 0.3f;
            } else if (dist < max_dist) {
                const float angle_diff = fabsf(atan2f(dy, dx));
                if (angle_diff < half_cone) {
                    float angle_factor = cosf((angle_diff / half_cone) * (float)(M_PI * 0.5));
                    angle_factor *= angle_factor;

                    float dist_factor = 1.0f - (dist / max_dist);
                    dist_factor = dist_factor * sqrtf(dist_factor);
                    light = angle_factor * dist_factor;
                }
            }

            if (light > 1.0f) light = 1.0f;
            if (light < 0.0f) light = 0.0f;
            s_light_lut[y][x] = (uint8_t)(light * 255.0f);
        }
    }
}

static uint16_t read_wram_u16(const GBContext* ctx, size_t offset) {
    return (uint16_t)(ctx->wram[offset] | ((uint16_t)ctx->wram[offset + 1u] << 8));
}

bool lighting_get_player_screen_position(GBContext* ctx, int width, int height,
                                         int* out_x, int* out_y) {
    if (!out_x || !out_y || width <= 0 || height <= 0) return false;

    *out_x = width / 2;
    *out_y = height / 2;
    if (!ctx || !ctx->wram) return false;

    // C1BD/C1BF are Gaiden's camera focus; focus-camera yields the true player screen anchor even when room bounds clamp the camera.
    const uint16_t focus_x = read_wram_u16(ctx, 0x01BDu);
    const uint16_t focus_y = read_wram_u16(ctx, 0x01BFu);
    const uint16_t camera_x = read_wram_u16(ctx, 0x01C5u);
    const uint16_t camera_y = read_wram_u16(ctx, 0x01C7u);

    const int native_x = (int)(int16_t)(focus_x - camera_x);
    const int native_y = (int)(int16_t)(focus_y - camera_y);
    const int wide_offset = (width - 160) / 2;

    // Fall back to the centered light if camera state is stale during a screen transition.
    if (native_x < -32 || native_x > 192 || native_y < -32 || native_y > 176) {
        return false;
    }

    *out_x = native_x + wide_offset;
    *out_y = native_y;
    return true;
}

static uint8_t sample_light_lut(int dx, int dy, PlayerFacingDir dir) {
    int forward;
    int side;
    switch (dir) {
        case DIR_LEFT:  forward = -dx; side = dy; break;
        case DIR_DOWN:  forward = dy;  side = dx; break;
        case DIR_UP:    forward = -dy; side = dx; break;
        case DIR_RIGHT:
        default:        forward = dx;  side = dy; break;
    }

    if (forward < -LIGHT_LUT_RADIUS || forward > LIGHT_LUT_RADIUS ||
        side < -LIGHT_LUT_RADIUS || side > LIGHT_LUT_RADIUS) {
        return 0;
    }
    return s_light_lut[side + LIGHT_LUT_RADIUS][forward + LIGHT_LUT_RADIUS];
}

void lighting_init(void) {
    s_player_dir = DIR_DOWN;
    s_frame_counter = 0;
    s_cached_cone_angle = -1;
    s_cached_cone_dist = -1;
}

void lighting_update_player_dir(uint8_t dpad_state) {
    // dpad_state is active LOW: bit 0: Right, 1: Left, 2: Up, 3: Down
    if (!(dpad_state & 0x01)) s_player_dir = DIR_RIGHT;
    else if (!(dpad_state & 0x02)) s_player_dir = DIR_LEFT;
    else if (!(dpad_state & 0x04)) s_player_dir = DIR_UP;
    else if (!(dpad_state & 0x08)) s_player_dir = DIR_DOWN;
}

PlayerFacingDir lighting_get_player_dir(void) {
    return s_player_dir;
}

/*
 * A window layer that starts at the top of the screen is a full-screen UI -
 * the title screen, the save/load menu, the inventory. Verified on the title
 * screen: LCDC=0xE7 (window enabled), WY=0, WX=7, i.e. the menu itself is
 * drawn on the window layer covering all 144 lines. A dialogue box, by
 * contrast, is anchored near the bottom.
 */
static bool is_full_screen_ui(const GBContext* ctx) {
    uint8_t lcdc = ctx->io[0x40];
    if (!(lcdc & 0x20)) {
        return false; // window disabled
    }
    if (ctx->io[0x4B] > 166) {
        return false; // window pushed off the right edge
    }
    return ctx->io[0x4A] <= FULLSCREEN_UI_MAX_WY;
}

static bool is_exploration_gameplay(const GBContext* ctx) {
    if (!ctx || !ctx->oam) return false;

    // 1. LCD must be enabled with Sprites active
    uint8_t lcdc = ctx->io[0x40];
    if ((lcdc & 0x82) != 0x82) {
        return false;
    }

    // 2. The game's own UI screens are not gameplay
    if (gb_state_is_ui_screen(ctx) || is_full_screen_ui(ctx)) {
        return false;
    }

    // 3. Active sprites present on screen
    for (int i = 0; i < 40; i++) {
        uint8_t y = ctx->oam[i * 4];
        uint8_t x = ctx->oam[i * 4 + 1];
        if (y >= 16 && y <= 160 && x >= 8 && x <= 168) {
            return true;
        }
    }

    return false;
}

bool lighting_is_active(GBContext* ctx) {
    return g_lighting_config.enabled && is_exploration_gameplay(ctx);
}

bool lighting_build_modulation_mask(GBContext* ctx, uint32_t* mask, int width, int height) {
    if (!mask || width <= 0 || height <= 0 || width > LUT_MAX_W || height > LUT_MAX_H ||
        !lighting_is_active(ctx)) {
        return false;
    }

    if (g_lighting_config.cone_angle_deg != s_cached_cone_angle ||
        g_lighting_config.cone_distance != s_cached_cone_dist) {
        rebuild_light_luts();
    }

    int anchor_x, anchor_y;
    lighting_get_player_screen_position(ctx, width, height, &anchor_x, &anchor_y);

    int intensity_256 = (g_lighting_config.intensity * 256) / 100;
    if (intensity_256 > 256) intensity_256 = 256;
    if (intensity_256 < 0) intensity_256 = 0;

    uint32_t ambient_256 = (uint32_t)(g_lighting_config.ambient_darkness * 256 / 100);
    if (ambient_256 > 256) ambient_256 = 256;

    for (int y = 0; y < height; ++y) {
        uint32_t* out = &mask[(size_t)y * (size_t)width];
        for (int x = 0; x < width; ++x) {
            const uint32_t light_val = sample_light_lut(x - anchor_x, y - anchor_y, s_player_dir);
            uint32_t light_contrib = (light_val * (uint32_t)intensity_256 * (256 - ambient_256)) >> 16;
            uint32_t total_light = ambient_256 + light_contrib;
            if (total_light > 256) total_light = 256;
            const uint8_t v = (uint8_t)((total_light * 255u + 128u) >> 8);
            out[x] = 0xFF000000u | ((uint32_t)v << 16) | ((uint32_t)v << 8) | v;
        }
    }
    return true;
}

void lighting_apply(GBContext* ctx, uint32_t* framebuffer, int width, int height) {
    if (!framebuffer || width <= 0 || height <= 0 || !lighting_is_active(ctx)) {
        return;
    }

    // The light LUT is only dimensioned for the supported viewport sizes.
    if (width > LUT_MAX_W || height > LUT_MAX_H) {
        return;
    }

    /*
     * There was a "battle mode" branch here keyed on wram[0x0900] > 0, applying
     * a flat darkening instead of the cone. $C900 is not a battle flag: the only
     * code in the ROM that touches it is a 0x50-byte save/restore memcpy, and it
     * reads as all zeroes outside gameplay, so the branch never fired reliably
     * and the cone ran during shooting sequences. Removed rather than left
     * guessing - see the state snapshots for finding the real flag.
     */

    s_frame_counter++;

    if (g_lighting_config.cone_angle_deg != s_cached_cone_angle ||
        g_lighting_config.cone_distance != s_cached_cone_dist) {
        rebuild_light_luts();
    }

    int anchor_x, anchor_y;
    lighting_get_player_screen_position(ctx, width, height, &anchor_x, &anchor_y);

    // Halogen bulb subtle flicker (integer scale 240-270 / 256)
    int flicker_factor = 256;
    if (g_lighting_config.flicker_enabled) {
        int noise = (int)(sin(s_frame_counter * 0.35) * 8.0);
        flicker_factor += noise;
    }

    // intensity is a 0-100 percentage; convert it to the 0-256 fixed-point scale
    // the blend below expects. Feeding the raw percentage in made the cone peak
    // at roughly a third of its intended brightness.
    int intensity_256 = (g_lighting_config.intensity * 256) / 100;
    int intensity_scaled = (intensity_256 * flicker_factor) >> 8;
    if (intensity_scaled > 256) intensity_scaled = 256;
    if (intensity_scaled < 0) intensity_scaled = 0;

    uint32_t ambient_256 = (uint32_t)(g_lighting_config.ambient_darkness * 256 / 100);
    if (ambient_256 > 256) ambient_256 = 256;

    // Use the precomputed cone with player-relative coordinates in the per-pixel lighting loop.
    for (int y = 0; y < height; y++) {
        uint32_t* row = &framebuffer[(size_t)y * (size_t)width];

        for (int x = 0; x < width; x++) {
            uint32_t p = row[x];
            uint32_t r = (p >> 16) & 0xFF;
            uint32_t g = (p >> 8) & 0xFF;
            uint32_t b = p & 0xFF;

            uint32_t light_val = sample_light_lut(x - anchor_x, y - anchor_y, s_player_dir); // 0 to 255
            uint32_t light_contrib = (light_val * (uint32_t)intensity_scaled * (256 - ambient_256)) >> 16;
            uint32_t total_light = ambient_256 + light_contrib;
            if (total_light > 256) total_light = 256;

            r = (r * total_light) >> 8;
            g = (g * total_light) >> 8;
            b = (b * total_light) >> 8;

            row[x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}
