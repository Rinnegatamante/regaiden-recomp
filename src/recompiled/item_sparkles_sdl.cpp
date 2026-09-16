#include "item_sparkles_sdl.h"

bool item_sparkles_sdl_render(SDL_Renderer* renderer, SDL_Texture** atlas,
                              const SDL_Rect* viewport, const ItemSparkleFrame* frame) {
    if (!renderer || !atlas || !viewport || !frame || viewport->w <= 0 || viewport->h <= 0) return false;
    if (!frame->count || !frame->opacity || !frame->width || !frame->height) return true;
    if (!*atlas) {
        uint32_t pixels[ITEM_SPARKLE_ATLAS_WIDTH * ITEM_SPARKLE_ATLAS_HEIGHT];
        item_sparkles_build_atlas(pixels);
        *atlas = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STATIC, ITEM_SPARKLE_ATLAS_WIDTH, ITEM_SPARKLE_ATLAS_HEIGHT);
        if (!*atlas) return false;
        if (SDL_UpdateTexture(*atlas, NULL, pixels, ITEM_SPARKLE_ATLAS_WIDTH * (int)sizeof(uint32_t)) < 0 ||
            SDL_SetTextureBlendMode(*atlas, SDL_BLENDMODE_BLEND) < 0 ||
            SDL_SetTextureScaleMode(*atlas, SDL_ScaleModeNearest) < 0) {
            SDL_DestroyTexture(*atlas);
            *atlas = NULL;
            return false;
        }
    }
    if (SDL_SetTextureAlphaMod(*atlas, frame->opacity) < 0) return false;
    const unsigned count = frame->count < ITEM_SPARKLE_MAX_ITEMS ? frame->count : ITEM_SPARKLE_MAX_ITEMS;
    for (unsigned draw = 0; draw < count * 2u; ++draw) {
        const unsigned i = draw / 2u;
        ItemSparkleQuad quad;
        if (!item_sparkles_get_quad(frame, i, draw & 1u, &quad)) continue;
        const SDL_Rect src = {quad.src_x, quad.src_y, quad.width, quad.height};
        SDL_Rect dst;
        dst.x = viewport->x + quad.x * viewport->w / frame->width;
        dst.y = viewport->y + quad.y * viewport->h / frame->height;
        dst.w = viewport->x + (quad.x + quad.width) * viewport->w / frame->width - dst.x;
        dst.h = viewport->y + (quad.y + quad.height) * viewport->h / frame->height - dst.y;
        if (SDL_RenderCopy(renderer, *atlas, &src, &dst) < 0) return false;
    }
    return true;
}
