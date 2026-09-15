#ifndef RE_VOXEL_SCENE_H
#define RE_VOXEL_SCENE_H

#include "gaiden_map.h"
#include "profiles.h"
#include "rasterizer.h"
#include <array>
#include <vector>

namespace revoxel {

struct SceneSettings {
    int wall_height = 28;
    int prop_height = 10;
    bool shadows = true;
};

struct Actor {
    static constexpr int MaxSize = 64;
    Vec3 feet;
    float floor_z = 0;
    int width = 0, height = 0;
    float left = 0;
    std::array<uint32_t, MaxSize * MaxSize> pixels{};
};

class Scene {
public:
    static constexpr int Side = 384;
    static constexpr int Cells = Side / 8;
    static constexpr size_t FaceBudget = 24000;
    bool update(const GaidenMap& map, const SceneSettings& settings, const Profiles& profiles);
    void draw(Rasterizer& rasterizer) const;
    static void draw_actor(Rasterizer& rasterizer, const Actor& actor, float yaw, bool shadow = true);
    int elevation(int world_x, int world_y) const;
    int support_elevation(int world_x, int world_y) const;
    int origin_x() const { return ox; }
    int origin_y() const { return oy; }
    const std::vector<uint8_t>& height_field() const { return heights; }
    const std::vector<uint32_t>& map_image() const { return atlas; }
    size_t face_count() const { return faces.size(); }
    size_t solid_pixels() const { return solid_count; }
    unsigned rebuild_count() const { return rebuilds; }
    const char* error() const { return failure; }
    void invalidate() { geometry_key = texture_key = 0; mesh_valid = false; }

private:
    std::array<MapCell, Cells * Cells> cells{};
    std::vector<uint8_t> heights, styles, shade, work, walkable;
    std::vector<int32_t> nearest_floor, cap_source, queue;
    std::vector<uint32_t> atlas, floor_atlas, cap_atlas;
    std::vector<Face> faces;
    int ox = 0, oy = 0;
    size_t solid_count = 0;
    uint64_t geometry_key = 0, texture_key = 0;
    unsigned rebuilds = 0;
    bool mesh_valid = false;
    const char* failure = "";
    bool build_geometry(const GaidenMap& map, const SceneSettings& settings, const Profiles& profiles);
    bool build_mesh();
    void build_surface_maps(bool shadows);
    void build_textures(const GaidenMap& map);
    bool append(const Face& face);
    int h(int x, int y) const;
};

}
#endif
