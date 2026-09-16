#ifndef ITEM_SPARKLES_H
#define ITEM_SPARKLES_H

#include "gbrt.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ITEM_SPARKLE_MAX_ITEMS 272
#define ITEM_SPARKLE_SIZE 7
#define ITEM_SPARKLE_LEVELS 8
#define ITEM_SPARKLE_ATLAS_WIDTH (ITEM_SPARKLE_SIZE * ITEM_SPARKLE_LEVELS)
#define ITEM_SPARKLE_ATLAS_HEIGHT ITEM_SPARKLE_SIZE

typedef struct {
    int16_t x;
    int16_t y;
    uint16_t object_id;
    uint8_t item_id;
    uint8_t level;
} ItemSparkle;

// Value-only snapshot: the render thread never reads guest memory.
typedef struct {
    ItemSparkle items[ITEM_SPARKLE_MAX_ITEMS];
    uint16_t count;
    uint16_t width;
    uint16_t height;
    int16_t clip_left;
    int16_t clip_top;
    int16_t clip_right;
    int16_t clip_bottom;
    int16_t window_y;
    int16_t lower_left;
    int16_t lower_right;
    uint8_t opacity;
} ItemSparkleFrame;

typedef struct {
    int src_x;
    int src_y;
    int x;
    int y;
    int width;
    int height;
} ItemSparkleQuad;

void item_sparkles_build_frame(const GBContext* ctx, int width, int height,
                               ItemSparkleFrame* frame);
bool item_sparkles_get_quad(const ItemSparkleFrame* frame, unsigned index, unsigned part,
                            ItemSparkleQuad* quad);
void item_sparkles_build_atlas(uint32_t* pixels);
void item_sparkles_blit(const ItemSparkleFrame* frame, uint32_t* pixels);

#ifdef __cplusplus
}
#endif

#endif
