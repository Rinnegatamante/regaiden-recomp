#include "scene.h"
#include "ppu.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace revoxel {

static uint64_t mix(uint64_t hash, uint64_t value) {
    return (hash ^ value) * UINT64_C(1099511628211);
}

static int floor_multiple(int value, int step) {
    return value >= 0 ? value / step * step : -((-value + step - 1) / step) * step;
}

bool Scene::update(const GaidenMap& map, const SceneSettings& settings, const Profiles& profiles) {
    try {
        ox = floor_multiple(map.camera_x + 80 - Side / 2 + 32, 64);
        oy = floor_multiple(map.camera_y + 72 - Side / 2 + 32, 64);
        uint64_t gkey = UINT64_C(14695981039346656037);
        for (int v : {ox, oy, map.room, map.width, map.height, settings.wall_height,
                      settings.prop_height, int(settings.shadows)}) gkey = mix(gkey, static_cast<uint32_t>(v));
        gkey = mix(gkey, profiles.revision());
        uint64_t tkey = gkey;
        const auto* ppu = static_cast<const GBPPU*>(map.context->ppu);
        for (uint8_t b : ppu->bg_palette_ram) tkey = mix(tkey, b);
        tkey = mix(tkey, ppu->lcdc & 0x10);
        std::array<uint64_t, 768> tile_hashes{};
        for (int bank = 0; bank < 2; ++bank) {
            for (int tile = 0; tile < 384; ++tile) {
                uint64_t key = UINT64_C(14695981039346656037);
                const uint8_t* data = map.context->vram + bank * 0x2000 + tile * 16;
                for (int i = 0; i < 16; ++i) key = mix(key, data[i]);
                tile_hashes[bank * 384 + tile] = key;
            }
        }
        int visible_cells = 0;
        for (int y = 0; y < Cells; ++y) {
            for (int x = 0; x < Cells; ++x) {
                MapCell& c = cells[y * Cells + x];
                const int tx = ox / 8 + x, ty = oy / 8 + y;
                const bool valid = map.cell(tx, ty, c);
                if (!valid && tx >= 0 && ty >= 0 && tx < map.width * 2 && ty < map.height * 2) {
                    failure = "Invalid map or collision address"; return false;
                }
                if (!valid) c = {};
                gkey = mix(gkey, c.metatile | (unsigned(c.visible) << 16) | (unsigned(c.collision_type) << 17));
                for (uint8_t row : c.collision) gkey = mix(gkey, row);
                if (c.visible) ++visible_cells;

                // Preserve live tile replacements in the native viewport; the ROM map supplies the surrounding world.
                const int sx = tx * 8 - map.camera_x, sy = ty * 8 - map.camera_y;
                if (c.visible && sx >= -7 && sx < 160 && sy >= -7 && sy < 144) {
                    const int bg = (ppu->lcdc & 8) ? 0x1c00 : 0x1800;
                    const int index = (((sy + ppu->scy) & 255) / 8) * 32 + (((sx + ppu->scx) & 255) / 8);
                    c.tile = map.context->vram[bg + index];
                    c.attributes = map.context->vram[0x2000 + bg + index];
                }
                const int tile = (ppu->lcdc & 0x10) ? c.tile : 256 + static_cast<int8_t>(c.tile);
                const int bank = (c.attributes & 8) ? 1 : 0;
                tkey = mix(tkey, c.tile | (unsigned(c.attributes) << 8) | (unsigned(c.visible) << 16));
                if (c.visible) tkey = mix(tkey, tile_hashes[bank * 384 + tile]);
            }
        }
        if (visible_cells < 4) { failure = "No visible world layer"; return false; }
        const bool geometry_changed = gkey != geometry_key;
        if (geometry_changed) {
            mesh_valid = build_geometry(map, settings, profiles);
            if (!mesh_valid) {
                geometry_key = gkey;
                return false;
            }
            geometry_key = gkey;
            ++rebuilds;
        } else if (!mesh_valid) {
            return false;
        }
        if (geometry_changed || tkey != texture_key) {
            build_textures(map);
            texture_key = tkey;
        }
        failure = "";
        return true;
    } catch (const std::bad_alloc&) {
        failure = "Voxel allocation failed";
        invalidate();
        return false;
    }
}

int Scene::h(int x, int y) const {
    return x < 0 || y < 0 || x >= Side || y >= Side ? 0 : heights[y * Side + x];
}

int Scene::elevation(int x, int y) const {
    return heights.empty() ? 0 : h(x - ox, y - oy);
}

int Scene::support_elevation(int x, int y) const {
    x -= ox; y -= oy;
    if (walkable.empty() || x < 0 || y < 0 || x >= Side || y >= Side) return 0;
    return walkable[y * Side + x] ? h(x, y) : 0;
}

bool Scene::build_geometry(const GaidenMap& map, const SceneSettings& settings, const Profiles& profiles) {
    constexpr size_t count = Side * Side;
    heights.assign(count, 0);
    styles.assign(count, 0);
    walkable.assign(count, 0);
    work.assign(count, 0);
    queue.resize(count);
    for (int cy = 0; cy < Cells; ++cy) {
        for (int cx = 0; cx < Cells; ++cx) {
            const MapCell& c = cells[cy * Cells + cx];
            if (!c.visible || c.collision_type >= 4) continue;
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    heights[(cy * 8 + y) * Side + cx * 8 + x] = (c.collision[y] & (0x80 >> x)) ? 1 : 0;
        }
    }

    // Collision fixes the footprint exactly; topology supplies conservative default heights, not tile colours or priority bits.
    for (int seed = 0; seed < Side * Side; ++seed) {
        if (!heights[seed] || work[seed]) continue;
        int head = 0, tail = 1;
        queue[0] = seed; work[seed] = 1;
        int minx = seed % Side, maxx = minx, miny = seed / Side, maxy = miny;
        bool boundary = false;
        while (head < tail) {
            const int p = queue[head++], x = p % Side, y = p / Side;
            minx = std::min(minx, x); maxx = std::max(maxx, x);
            miny = std::min(miny, y); maxy = std::max(maxy, y);
            boundary |= ox + x == 0 || oy + y == 0 || ox + x == map.width * 16 - 1 || oy + y == map.height * 16 - 1;
            const int neighbors[4] = {x > 0 ? p - 1 : -1, x + 1 < Side ? p + 1 : -1,
                                      y > 0 ? p - Side : -1, y + 1 < Side ? p + Side : -1};
            for (int n : neighbors) {
                if (n >= 0 && heights[n] && !work[n]) { work[n] = 1; queue[tail++] = n; }
            }
        }
        const int width = maxx - minx + 1, depth = maxy - miny + 1;
        const bool wall = boundary || (std::max(width, depth) >= 48 && std::min(width, depth) <= 24) ||
                          (tail >= 1536 && std::max(width, depth) >= 64);
        const int elevation = std::clamp(wall ? settings.wall_height : settings.prop_height, 1, 64);
        for (int i = 0; i < tail; ++i) {
            heights[queue[i]] = static_cast<uint8_t>(elevation);
            styles[queue[i]] = wall ? 1 : 2;
        }
    }

    work = styles;
    for (const ShapeRule& rule : profiles.entries()) {
        if (rule.room >= 0 && rule.room != map.room) continue;
        const int x0 = rule.metatile >= 0 ? 0 : std::max(0, rule.x - ox);
        const int y0 = rule.metatile >= 0 ? 0 : std::max(0, rule.y - oy);
        const int x1 = rule.metatile >= 0 ? Side : std::min(Side, rule.x + rule.width - ox);
        const int y1 = rule.metatile >= 0 ? Side : std::min(Side, rule.y + rule.depth - oy);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                const MapCell& cell = cells[(y / 8) * Cells + x / 8];
                const int p = y * Side + x;
                if (!cell.visible || (rule.metatile >= 0 && cell.metatile != rule.metatile)) continue;
                if (rule.metatile >= 0 && !rule.fill && (cell.collision_type >= 4 ||
                    !(cell.collision[y & 7] & (0x80 >> (x & 7))))) continue;
                const int dx = rule.metatile >= 0 ? ((ox + x) & 15) : ox + x - rule.x;
                const int dy = rule.metatile >= 0 ? ((oy + y) & 15) : oy + y - rule.y;
                heights[p] = static_cast<uint8_t>(shaped_height(rule.shape, rule.height, dx, dy,
                    rule.metatile >= 0 ? 16 : rule.width, rule.metatile >= 0 ? 16 : rule.depth));
                walkable[p] = rule.shape == Shape::StairsN || rule.shape == Shape::StairsS || rule.shape == Shape::Platform;
                if (rule.shape == Shape::Auto)
                    styles[p] = work[p] ? work[p] : 2;
                else
                    styles[p] = rule.shape == Shape::Wall || rule.shape == Shape::Rail ? 1 : 2;
            }
        }
    }
    solid_count = 0;
    for (uint8_t height : heights) solid_count += height != 0;
    build_surface_maps(settings.shadows);
    failure = "";
    return build_mesh();
}

void Scene::build_surface_maps(bool shadows) {
    const int count = Side * Side;
    nearest_floor.assign(count, -1);
    cap_source.resize(count);
    shade.assign(count, 255);
    int head = 0, tail = 0;
    for (int y = 0; y < Side; ++y) {
        for (int x = 0; x < Side; ++x) {
            const int p = y * Side + x;
            cap_source[p] = p;
            if (!cells[(y / 8) * Cells + x / 8].visible) continue;
            if (!heights[p]) { nearest_floor[p] = p; queue[tail++] = p; }
            if (heights[p] && styles[p] == 1) {
                int top = y;
                while (top > 0 && y - top < 31 && h(x, top - 1) > 0 && styles[(top - 1) * Side + x] == 1) --top;
                cap_source[p] = (top + std::min(1, y - top)) * Side + x;
            }
        }
    }
    while (head < tail) {
        const int p = queue[head++], x = p % Side, y = p / Side;
        const int neighbors[4] = {x > 0 ? p - 1 : -1, x + 1 < Side ? p + 1 : -1,
                                  y > 0 ? p - Side : -1, y + 1 < Side ? p + Side : -1};
        for (int n : neighbors) {
            if (n < 0 || nearest_floor[n] >= 0) continue;
            const int nx = n % Side, ny = n / Side;
            if (!cells[(ny / 8) * Cells + nx / 8].visible) continue;
            nearest_floor[n] = nearest_floor[p]; queue[tail++] = n;
        }
    }
    if (!shadows) return;
    for (int p = 0; p < count; ++p) work[p] = heights[p] ? 0 : 32;
    for (int y = 0; y < Side; ++y) {
        for (int x = 0; x < Side; ++x) {
            const int p = y * Side + x;
            if (x) work[p] = std::min(int(work[p]), int(work[p - 1]) + 1);
            if (y) work[p] = std::min(int(work[p]), int(work[p - Side]) + 1);
        }
    }
    for (int y = Side - 1; y >= 0; --y) {
        for (int x = Side - 1; x >= 0; --x) {
            const int p = y * Side + x;
            if (x + 1 < Side) work[p] = std::min(int(work[p]), int(work[p + 1]) + 1);
            if (y + 1 < Side) work[p] = std::min(int(work[p]), int(work[p + Side]) + 1);
            shade[p] = static_cast<uint8_t>(std::min(255, 174 + int(work[p]) * 12));
        }
    }
    for (int y = 0; y < Side; ++y) {
        for (int x = 0; x < Side; ++x) {
            const int height = h(x, y);
            for (int z = 2; z <= height; z += 2) {
                const int sx = x + z / 2, sy = y + z * 3 / 4;
                if (sx >= Side || sy >= Side) break;
                if (!h(sx, sy)) shade[sy * Side + sx] = std::min<int>(shade[sy * Side + sx], z + 3 < height ? 196 : 222);
            }
        }
    }
}

bool Scene::append(const Face& face) {
    if (faces.size() >= FaceBudget) { failure = "Visible geometry exceeds the 24000-face budget"; return false; }
    faces.push_back(face);
    return true;
}

bool Scene::build_mesh() {
    faces.clear();
    faces.reserve(4096);
    const float x0 = float(ox), y0 = float(oy), x1 = float(ox + Side), y1 = float(oy + Side);
    if (!append(rectangle({x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0}, {0, 0, 1},
        {0, 0}, {Side, 0}, {Side, Side}, {0, Side}, 256, 1))) return false;
    std::fill(work.begin(), work.end(), 0);
    for (int y = 0; y < Side; ++y) {
        for (int x = 0; x < Side; ++x) {
            const int p = y * Side + x, height = heights[p], style = styles[p];
            if (!height || work[p]) continue;
            int endx = x + 1;
            while (endx < Side && !work[y * Side + endx] && h(endx, y) == height && styles[y * Side + endx] == style) ++endx;
            int endy = y + 1;
            for (; endy < Side; ++endy) {
                bool same = true;
                for (int xx = x; xx < endx; ++xx) {
                    const int q = endy * Side + xx;
                    if (work[q] || heights[q] != height || styles[q] != style) { same = false; break; }
                }
                if (!same) break;
            }
            for (int yy = y; yy < endy; ++yy)
                std::fill(work.begin() + yy * Side + x, work.begin() + yy * Side + endx, 1);
            const float a = float(ox + x), b = float(ox + endx), c = float(oy + y), d = float(oy + endy), z = float(height);
            if (!append(rectangle({a, c, z}, {b, c, z}, {b, d, z}, {a, d, z}, {0, 0, 1},
                {float(x), float(y)}, {float(endx), float(y)}, {float(endx), float(endy)}, {float(x), float(endy)},
                256, style == 1 ? 2 : 0, style == 1))) return false;
        }
    }
    struct Edge { int high, low, style, depth; };
    for (int direction = 0; direction < 4; ++direction) {
        const bool horizontal = direction < 2;
        const bool positive = direction == 0 || direction == 2;
        for (int major = 0; major < Side; ++major) {
            auto edge = [&](int minor) {
                const int x = horizontal ? minor : major, y = horizontal ? major : minor;
                const int nx = horizontal ? x : x + (positive ? 1 : -1);
                const int ny = horizontal ? y + (positive ? 1 : -1) : y;
                Edge e{h(x, y), h(nx, ny), styles[y * Side + x], 1};
                if (e.high <= e.low) return e;
                if (e.style == 1) {
                    for (int step = 1; step < std::min(32, e.high); ++step) {
                        const int sx = horizontal ? x : x + (positive ? -step : step);
                        const int sy = horizontal ? y + (positive ? -step : step) : y;
                        if (!h(sx, sy) || styles[sy * Side + sx] != e.style) break;
                        ++e.depth;
                    }
                }
                return e;
            };
            for (int minor = 0; minor < Side;) {
                const Edge e = edge(minor);
                if (e.high <= e.low) { ++minor; continue; }
                int end = minor + 1;
                while (end < Side) {
                    const Edge next = edge(end);
                    if (next.high != e.high || next.low != e.low || next.style != e.style || next.depth != e.depth) break;
                    ++end;
                }
                const float high = float(e.high), low = float(e.low);
                const float base = major + (positive ? 0.99f : 0.01f);
                const float sign = positive ? -1.0f : 1.0f;
                const float uv_low = base + sign * (e.depth - 1) * low / high;
                const float uv_high = base + sign * (e.depth - 1);
                const float m0 = float(minor), m1 = float(end);
                Face f;
                if (horizontal) {
                    const float a = float(ox + minor), b = float(ox + end), line = float(oy + major + (positive ? 1 : 0));
                    f = rectangle({a, line, low}, {b, line, low}, {b, line, high}, {a, line, high},
                        {0, positive ? 1.0f : -1.0f, 0}, {m0, uv_low}, {m1, uv_low}, {m1, uv_high}, {m0, uv_high},
                        positive ? 234 : 190, 0, e.style == 1);
                } else {
                    const float a = float(oy + minor), b = float(oy + end), line = float(ox + major + (positive ? 1 : 0));
                    f = rectangle({line, a, low}, {line, b, low}, {line, b, high}, {line, a, high},
                        {positive ? 1.0f : -1.0f, 0, 0}, {uv_low, m0}, {uv_low, m1}, {uv_high, m1}, {uv_high, m0},
                        positive ? 184 : 220, 0, e.style == 1);
                }
                if (!append(f)) return false;
                minor = end;
            }
        }
    }
    return true;
}

void Scene::build_textures(const GaidenMap& map) {
    atlas.resize(Side * Side);
    floor_atlas.resize(Side * Side);
    cap_atlas.resize(Side * Side);
    for (int cy = 0; cy < Cells; ++cy) {
        for (int cx = 0; cx < Cells; ++cx) {
            const MapCell& c = cells[cy * Cells + cx];
            for (int y = 0; y < 8; ++y)
                for (int x = 0; x < 8; ++x)
                    atlas[(cy * 8 + y) * Side + cx * 8 + x] = map.background_pixel(c, x, y);
        }
    }
    for (int y = 0; y < Side; ++y) {
        for (int x = 0; x < Side; ++x) {
            const int p = y * Side + x;
            int source = p;
            if (heights[p]) {
                source = nearest_floor[p];
                if (source >= 0) {
                    const int nx = ((source % Side) & ~7) | (x & 7);
                    const int ny = ((source / Side) & ~7) | (y & 7);
                    if (!h(nx, ny) && atlas[ny * Side + nx]) source = ny * Side + nx;
                }
            }
            floor_atlas[p] = source >= 0 ? modulate(atlas[source], unsigned(shade[p]) + 1) : 0;
            cap_atlas[p] = atlas[cap_source[p]];
        }
    }
}

void Scene::draw(Rasterizer& raster) const {
    const Texture textures[3] = {{atlas.data(), Side, Side}, {floor_atlas.data(), Side, Side}, {cap_atlas.data(), Side, Side}};
    for (const Face& face : faces) raster.draw(face, textures[face.texture]);
}

void Scene::draw_actor(Rasterizer& raster, const Actor& actor, float yaw, bool shadow) {
    if (actor.width <= 0 || actor.height <= 0 || actor.width > Actor::MaxSize || actor.height > Actor::MaxSize) return;
    const float angle = yaw * 0.01745329251994329577f;
    const float rx = std::cos(angle), ry = std::sin(angle), nx = -ry, ny = rx;
    auto point = [&](float x, float z, float depth) -> Vec3 {
        return {actor.feet.x + rx * x + nx * depth, actor.feet.y + ry * x + ny * depth, actor.feet.z + z};
    };
    if (shadow) {
        static const auto shadow_pixels = [] {
            std::array<uint32_t, 32 * 16> result{};
            for (int y = 0; y < 16; ++y) for (int x = 0; x < 32; ++x) {
                const float dx = (x - 15.5f) / 15.5f, dy = (y - 7.5f) / 7.5f;
                const float alpha = std::max(0.0f, 1 - dx * dx - dy * dy);
                result[y * 32 + x] = static_cast<uint32_t>(alpha * alpha * 100) << 24;
            }
            return result;
        }();
        const float x = actor.feet.x, y = actor.feet.y, z = actor.floor_z + 0.03f;
        const float radius = std::max(5.0f, actor.width * 0.4f);
        raster.draw(rectangle({x - radius, y - 4, z}, {x + radius, y - 4, z}, {x + radius, y + 4, z}, {x - radius, y + 4, z},
            {0, 0, 1}, {0, 0}, {32, 0}, {32, 16}, {0, 16}), {shadow_pixels.data(), 32, 16});
    }
    auto pixel = [&](int x, int y) -> uint32_t {
        return x < 0 || y < 0 || x >= actor.width || y >= actor.height ? 0 : actor.pixels[y * Actor::MaxSize + x];
    };
    auto solid_face = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 normal, uint32_t color, unsigned shade) {
        raster.draw(rectangle(a, b, c, d, normal, {0, 0}, {0, 0}, {0, 0}, {0, 0}, shade), {&color, 1, 1});
    };
    std::array<uint8_t, Actor::MaxSize * Actor::MaxSize> used{};
    for (int y = 0; y < actor.height; ++y) {
        for (int x = 0; x < actor.width; ++x) {
            const uint32_t color = pixel(x, y);
            if (!(color >> 24)) continue;
            const float left = actor.left + x, right = left + 1, top = float(actor.height - y), bottom = top - 1;
            if (!(pixel(x, y - 1) >> 24))
                solid_face(point(left, top, -1), point(right, top, -1), point(right, top, 1), point(left, top, 1), {0, 0, 1}, color, 246);
            if (!(pixel(x - 1, y) >> 24))
                solid_face(point(left, bottom, -1), point(left, bottom, 1), point(left, top, 1), point(left, top, -1), {-rx, -ry, 0}, color, 205);
            if (!(pixel(x + 1, y) >> 24))
                solid_face(point(right, bottom, 1), point(right, bottom, -1), point(right, top, -1), point(right, top, 1), {rx, ry, 0}, color, 185);
            if (used[y * Actor::MaxSize + x]) continue;
            int endx = x + 1;
            while (endx < actor.width && !used[y * Actor::MaxSize + endx] && pixel(endx, y) == color) ++endx;
            int endy = y + 1;
            for (; endy < actor.height; ++endy) {
                bool same = true;
                for (int xx = x; xx < endx; ++xx)
                    if (used[endy * Actor::MaxSize + xx] || pixel(xx, endy) != color) { same = false; break; }
                if (!same) break;
            }
            for (int yy = y; yy < endy; ++yy)
                std::fill(used.begin() + yy * Actor::MaxSize + x, used.begin() + yy * Actor::MaxSize + endx, 1);
            const float far_right = actor.left + endx, low = float(actor.height - endy);
            solid_face(point(left, low, 1), point(far_right, low, 1), point(far_right, top, 1), point(left, top, 1), {nx, ny, 0}, color, 256);
        }
    }
}

}
