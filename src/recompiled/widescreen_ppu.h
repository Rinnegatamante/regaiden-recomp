#ifndef WIDESCREEN_PPU_H
#define WIDESCREEN_PPU_H

#include "gbrt.h"
#include "config_ini.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GB_NATIVE_WIDTH 160
#define GB_NATIVE_HEIGHT 144

#define GB_WIDESCREEN_WIDTH 256
#define GB_WIDESCREEN_HEIGHT 144
#define GB_WIDESCREEN_BUGGY_RIGHT_COLUMN_WIDTH 24
#define GB_WIDESCREEN_CONTENT_WIDTH (GB_WIDESCREEN_WIDTH - GB_WIDESCREEN_BUGGY_RIGHT_COLUMN_WIDTH)
#define GB_WIDESCREEN_SIDE_BAND_WIDTH ((GB_WIDESCREEN_WIDTH - GB_WIDESCREEN_CONTENT_WIDTH) / 2)

#define GB_MAX_FRAMEBUFFER_SIZE (GB_WIDESCREEN_WIDTH * GB_WIDESCREEN_HEIGHT)

extern uint32_t g_wide_framebuffer[GB_MAX_FRAMEBUFFER_SIZE];

/**
 * @brief Get the current target framebuffer width according to widescreen mode.
 */
int widescreen_get_target_width(void);

/**
 * @brief Get the current target framebuffer height.
 */
int widescreen_get_target_height(void);

/* Host-side entity capture used to extend Gaiden's native 160px sprite culling. */
void widescreen_entity_frame_begin(GBContext* ctx);
void widescreen_entity_capture_begin(GBContext* ctx, uint8_t entity_page);
void widescreen_entity_capture_end(GBContext* ctx);
bool widescreen_entity_should_extend_x(int16_t relative_x);

/**
 * @brief Render the expanded True Widescreen or Native frame from Game Boy VRAM.
 * @param ctx Game Boy context
 * @param native_fb Dot-accurate 160x144 frame from the PPU, copied verbatim into
 *                  the centre of the output. Pass NULL to sample the PPU directly.
 * @param out_fb Output buffer (at least GB_MAX_FRAMEBUFFER_SIZE uint32_t)
 * @param out_width Output width (160 or 256)
 * @param out_height Output height (144)
 */
void widescreen_render_frame(GBContext* ctx, const uint32_t* native_fb, uint32_t* out_fb,
                             int* out_width, int* out_height);

#ifdef __cplusplus
}
#endif

#endif /* WIDESCREEN_PPU_H */
