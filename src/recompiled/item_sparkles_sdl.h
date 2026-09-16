#ifndef ITEM_SPARKLES_SDL_H
#define ITEM_SPARKLES_SDL_H

#include "item_sparkles.h"
#include <SDL.h>

// The caller owns the atlas texture and destroys it when its SDL renderer resets.
bool item_sparkles_sdl_render(SDL_Renderer* renderer, SDL_Texture** atlas,
                              const SDL_Rect* viewport, const ItemSparkleFrame* frame);

#endif
