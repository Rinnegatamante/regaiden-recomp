#ifndef RE_VOXEL_RASTERIZER_H
#define RE_VOXEL_RASTERIZER_H

#include <cstdint>
#include <vector>

namespace revoxel {

struct Vec2 { float x = 0, y = 0; };
struct Vec3 { float x = 0, y = 0, z = 0; };
struct Texture { const uint32_t* pixels = nullptr; int width = 0, height = 0; };

struct Camera {
    Vec3 target;
    float yaw = 0;
    float pitch = 55;
    float zoom = 1;
    float distance = 360;
};

struct Face {
    Vec3 points[4];
    Vec2 uv[4];
    Vec3 normal;
    uint16_t shade = 256;
    uint8_t texture = 0;
    bool cutaway = false;
};

class Rasterizer {
public:
    bool begin(int width, int height, const Camera& camera, uint32_t clear = 0xff101418u);
    void draw(const Face& face, Texture texture);
    Vec3 project(Vec3 point) const;
    void set_cutaway(Vec3 feet, float actor_height, bool enabled);
    const uint32_t* pixels() const { return colors.data(); }
    const std::vector<float>& depth_buffer() const { return depths; }
    int width() const { return w; }
    int height() const { return h; }
    unsigned triangles = 0;
    unsigned fragments = 0;

private:
    struct Vertex { float x, y, z, u, v; };
    std::vector<uint32_t> colors;
    std::vector<float> depths;
    int w = 0, h = 0;
    Camera camera;
    float cy = 1, sy = 0, cp = 1, sp = 0, focal = 1;
    bool cut = false;
    float cut_x = 0, cut_y = 0, cut_rx = 1, cut_ry = 1, cut_q = 0;
    Vec3 view(Vec3 point) const;
    void triangle(Vertex a, Vertex b, Vertex c, Texture texture, unsigned shade, bool cutaway);
};

uint32_t modulate(uint32_t color, unsigned shade);
bool fit_camera_coverage(Camera& camera, int width, int height, float radius, float maximum_height);
Face rectangle(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal,
               Vec2 uv0, Vec2 uv1, Vec2 uv2, Vec2 uv3,
               unsigned shade = 256, unsigned texture = 0, bool cutaway = false);

}
#endif
