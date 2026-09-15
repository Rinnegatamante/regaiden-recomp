#include "rasterizer.h"
#include <algorithm>
#include <cmath>
#include <new>

namespace revoxel {

uint32_t modulate(uint32_t color, unsigned shade) {
    if (shade == 256) return color;
    const unsigned r = std::min(255u, (((color >> 16) & 255u) * shade) >> 8);
    const unsigned g = std::min(255u, (((color >> 8) & 255u) * shade) >> 8);
    const unsigned b = std::min(255u, ((color & 255u) * shade) >> 8);
    return (color & 0xff000000u) | (r << 16) | (g << 8) | b;
}

Face rectangle(Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal,
               Vec2 t0, Vec2 t1, Vec2 t2, Vec2 t3,
               unsigned shade, unsigned texture, bool cutaway) {
    Face f{{a, b, c, d}, {t0, t1, t2, t3}, normal};
    f.shade = static_cast<uint16_t>(std::min(shade, 512u));
    f.texture = static_cast<uint8_t>(texture);
    f.cutaway = cutaway;
    return f;
}

bool fit_camera_coverage(Camera& camera, int width, int height, float radius, float maximum_height) {
    if (width < 16 || height < 16 || radius <= 0 || maximum_height < 0 ||
        !std::isfinite(camera.pitch) || !std::isfinite(camera.yaw) ||
        !std::isfinite(camera.zoom) || !std::isfinite(camera.distance) ||
        !std::isfinite(radius) || !std::isfinite(maximum_height)) return false;
    const float radians = 0.01745329251994329577f;
    const float cp = std::cos(camera.pitch * radians), sp = std::sin(camera.pitch * radians);
    const float cy = std::cos(camera.yaw * radians), sy = std::sin(camera.yaw * radians);
    auto covered = [&](float zoom) {
        const float focal = width * camera.distance * zoom / 224.0f;
        if (focal <= 0) return false;
        for (float z : {0.0f, maximum_height}) {
            for (float screen_y : {-1.0f, float(height + 1)}) {
                const float dy = (screen_y - height * 0.55f) / focal;
                const float denominator = sp + dy * cp;
                if (denominator <= 0) return false;
                const float forward = (dy * (camera.distance - sp * z) + cp * z) / denominator;
                const float depth = camera.distance - cp * forward - sp * z;
                if (depth <= 8) return false;
                for (float screen_x : {-1.0f, float(width + 1)}) {
                    const float horizontal = (screen_x - width * 0.5f) * depth / focal;
                    if (std::abs(cy * horizontal - sy * forward) > radius ||
                        std::abs(sy * horizontal + cy * forward) > radius) return false;
                }
            }
        }
        return true;
    };
    float low = std::clamp(camera.zoom, 0.8f, 2.0f), high = 2.0f;
    if (covered(low)) { camera.zoom = low; return true; }
    if (!covered(high)) return false;
    for (int i = 0; i < 12; ++i) {
        const float middle = (low + high) * 0.5f;
        if (covered(middle)) high = middle; else low = middle;
    }
    camera.zoom = high;
    return true;
}

bool Rasterizer::begin(int width, int height, const Camera& cam, uint32_t clear) {
    if (width < 16 || height < 16 || width > 640 || height > 480 ||
        !std::isfinite(cam.target.x) || !std::isfinite(cam.target.y) ||
        !std::isfinite(cam.target.z) || !std::isfinite(cam.yaw) ||
        !std::isfinite(cam.pitch) || !std::isfinite(cam.zoom) ||
        !std::isfinite(cam.distance)) return false;
    try {
        colors.assign(static_cast<size_t>(width) * height, clear);
        depths.assign(static_cast<size_t>(width) * height, 0.0f);
    } catch (const std::bad_alloc&) {
        return false;
    }
    w = width; h = height; camera = cam;
    camera.pitch = std::clamp(cam.pitch, 35.0f, 85.0f);
    camera.zoom = std::clamp(cam.zoom, 0.8f, 2.0f);
    camera.distance = std::clamp(cam.distance, 128.0f, 1024.0f);
    const float radians = 0.01745329251994329577f;
    cy = std::cos(cam.yaw * radians); sy = std::sin(cam.yaw * radians);
    cp = std::cos(camera.pitch * radians); sp = std::sin(camera.pitch * radians);
    focal = w * camera.distance * camera.zoom / 224.0f;
    triangles = fragments = 0;
    cut = false;
    return true;
}

Vec3 Rasterizer::view(Vec3 p) const {
    const float dx = p.x - camera.target.x, dy = p.y - camera.target.y, dz = p.z - camera.target.z;
    const float horizontal = cy * dx + sy * dy;
    const float forward = -sy * dx + cy * dy;
    return {horizontal, sp * forward - cp * dz, camera.distance - cp * forward - sp * dz};
}

Vec3 Rasterizer::project(Vec3 p) const {
    const Vec3 v = view(p);
    const float q = 1.0f / std::max(v.z, 1.0f);
    return {w * 0.5f + focal * v.x * q, h * 0.55f + focal * v.y * q, q};
}

void Rasterizer::set_cutaway(Vec3 feet, float actor_height, bool enabled) {
    const Vec3 head = project({feet.x, feet.y, feet.z + actor_height});
    const Vec3 base = project(feet);
    cut_x = (head.x + base.x) * 0.5f;
    cut_y = (head.y + base.y) * 0.5f;
    cut_rx = std::max(14.0f, focal * base.z * 17.0f);
    cut_ry = std::max(18.0f, std::abs(base.y - head.y) * 0.65f + cut_rx * 0.5f);
    cut_q = base.z;
    cut = enabled;
}

void Rasterizer::draw(const Face& face, Texture texture) {
    if (!texture.pixels || texture.width < 1 || texture.height < 1 || !w || !h) return;
    const Vec3 eye{camera.target.x - sy * cp * camera.distance,
                   camera.target.y + cy * cp * camera.distance,
                   camera.target.z + sp * camera.distance};
    const Vec3& p = face.points[0];
    if (face.normal.x * (eye.x - p.x) + face.normal.y * (eye.y - p.y) +
        face.normal.z * (eye.z - p.z) < -0.001f) return;

    Vertex input[8], clipped[8];
    for (int i = 0; i < 4; ++i) {
        const Vec3 v = view(face.points[i]);
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return;
        input[i] = {v.x, v.y, v.z, face.uv[i].x, face.uv[i].y};
    }
    int count = 0;
    constexpr float near_plane = 8.0f;
    for (int i = 0; i < 4; ++i) {
        const Vertex& a = input[i];
        const Vertex& b = input[(i + 1) & 3];
        const bool ina = a.z >= near_plane, inb = b.z >= near_plane;
        if (ina) clipped[count++] = a;
        if (ina != inb) {
            const float t = (near_plane - a.z) / (b.z - a.z);
            clipped[count++] = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                near_plane, a.u + (b.u - a.u) * t, a.v + (b.v - a.v) * t};
        }
    }
    if (count < 3) return;
    for (int i = 0; i < count; ++i) {
        Vertex& v = clipped[i];
        const float q = 1.0f / v.z;
        v.x = w * 0.5f + focal * v.x * q;
        v.y = h * 0.55f + focal * v.y * q;
        v.z = q; v.u *= q; v.v *= q;
    }
    for (int i = 1; i + 1 < count; ++i)
        triangle(clipped[0], clipped[i], clipped[i + 1], texture, face.shade, face.cutaway);
}

void Rasterizer::triangle(Vertex a, Vertex b, Vertex c, Texture texture, unsigned shade, bool cutaway) {
    const Vertex vertices[3] = {a, b, c};
    const float low = std::min({a.y, b.y, c.y}), high = std::max({a.y, b.y, c.y});
    if (high <= 0 || low >= h || high - low < 0.0001f) return;
    const int first = static_cast<int>(std::ceil(std::clamp(low - 0.5f, 0.0f, float(h))));
    const int last = static_cast<int>(std::ceil(std::clamp(high - 0.5f, 0.0f, float(h))));
    ++triangles;
    static constexpr uint8_t dither[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};
    for (int y = first; y < last; ++y) {
        const float scan = y + 0.5f;
        Vertex edge[3];
        int intersections = 0;
        for (int e = 0; e < 3; ++e) {
            const Vertex& p = vertices[e];
            const Vertex& q = vertices[(e + 1) % 3];
            if (!((p.y <= scan && scan < q.y) || (q.y <= scan && scan < p.y))) continue;
            const float t = (scan - p.y) / (q.y - p.y);
            edge[intersections++] = {p.x + (q.x - p.x) * t, scan, p.z + (q.z - p.z) * t,
                                    p.u + (q.u - p.u) * t, p.v + (q.v - p.v) * t};
        }
        if (intersections != 2) continue;
        if (edge[1].x < edge[0].x) std::swap(edge[0], edge[1]);
        const float span = edge[1].x - edge[0].x;
        if (span < 0.0001f || edge[1].x <= 0 || edge[0].x >= w) continue;
        const int left = static_cast<int>(std::ceil(std::clamp(edge[0].x - 0.5f, 0.0f, float(w))));
        const int right = static_cast<int>(std::ceil(std::clamp(edge[1].x - 0.5f, 0.0f, float(w))));
        const float inv_span = 1.0f / span;
        const float dz = (edge[1].z - edge[0].z) * inv_span;
        const float du = (edge[1].u - edge[0].u) * inv_span;
        const float dv = (edge[1].v - edge[0].v) * inv_span;
        const float advance = left + 0.5f - edge[0].x;
        float q = edge[0].z + dz * advance;
        float u = edge[0].u + du * advance;
        float v = edge[0].v + dv * advance;
        size_t offset = static_cast<size_t>(y) * w + left;
        for (int x = left; x < right; ++x, ++offset, q += dz, u += du, v += dv) {
            if (q <= depths[offset] || q <= 0) continue;
            if (cut && cutaway && q > cut_q) {
                const float dx = (x - cut_x) / cut_rx, dy = (y - cut_y) / cut_ry;
                const float distance = dx * dx + dy * dy;
                if (distance < 1.0f && dither[(y & 3) * 4 + (x & 3)] > 2 + distance * 13.0f) continue;
            }
            const float reciprocal = 1.0f / q;
            const float tx = u * reciprocal, ty = v * reciprocal;
            if (tx < -0.01f || ty < -0.01f || tx >= texture.width || ty >= texture.height) continue;
            const int ix = std::max(0, static_cast<int>(tx));
            const int iy = std::max(0, static_cast<int>(ty));
            uint32_t color = texture.pixels[static_cast<size_t>(iy) * texture.width + ix];
            const unsigned alpha = color >> 24;
            if (!alpha) continue;
            color = modulate(color, shade);
            if (alpha < 255) {
                const uint32_t old = colors[offset];
                const unsigned inverse = 255 - alpha;
                const unsigned r = (((color >> 16) & 255u) * alpha + ((old >> 16) & 255u) * inverse) / 255;
                const unsigned g = (((color >> 8) & 255u) * alpha + ((old >> 8) & 255u) * inverse) / 255;
                const unsigned bl = ((color & 255u) * alpha + (old & 255u) * inverse) / 255;
                colors[offset] = 0xff000000u | (r << 16) | (g << 8) | bl;
            } else {
                colors[offset] = color;
                depths[offset] = q;
            }
            ++fragments;
        }
    }
}

}
