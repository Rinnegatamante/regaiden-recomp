#ifndef RE_VOXEL_GAIDEN_MAP_H
#define RE_VOXEL_GAIDEN_MAP_H

#include "gbrt.h"
#include <array>
#include <cstdint>

namespace revoxel {

struct MapCell {
    uint16_t metatile = 0;
    uint8_t tile = 0;
    uint8_t attributes = 0;
    uint8_t collision_type = 0;
    std::array<uint8_t, 8> collision{};
    bool visible = false;
};

// All readers are side-effect free: they never switch the guest's memory banks.
class GaidenMap {
public:
    bool open(const GBContext* context);
    bool metatile(int x, int y, uint16_t& value, bool* visible = nullptr) const;
    bool cell(int tile_x, int tile_y, MapCell& value) const;
    bool blocked(int pixel_x, int pixel_y, bool& value, uint8_t* type = nullptr) const;
    bool rom_byte(unsigned bank, unsigned address, uint8_t& value) const;
    uint32_t background_pixel(const MapCell& cell, int x, int y) const;
    uint32_t object_pixel(uint8_t tile, uint8_t attributes, int x, int y, int height) const;
    const uint8_t* entity(unsigned page) const;

    const GBContext* context = nullptr;
    int width = 0;
    int height = 0;
    int room = -1;
    int camera_x = 0;
    int camera_y = 0;
    int entity_bank = 1;
    uint8_t focus_page = 0;
    uint8_t layer_mask = 0xff;
    uint8_t flags = 0;

private:
    unsigned map_address = 0, map_bank = 0;
    unsigned tile_address = 0, attribute_address = 0, tile_bank = 0;
    unsigned collision_address = 0, collision_bank = 0;
    unsigned mask_address = 0, mask_bank = 0;
    uint32_t palette_pixel(bool object, uint8_t attributes, unsigned index) const;
};

uint16_t little_u16(const uint8_t* bytes);
uint32_t color555(uint16_t color);

}
#endif
