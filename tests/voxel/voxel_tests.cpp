#include "voxel/gaiden_map.h"
#include "voxel/profiles.h"
#include "voxel/rasterizer.h"
#include "voxel/scene.h"
#include "ppu.h"
#include "voxelizer.h"
#include "config_ini.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace revoxel;
static unsigned checks = 0;
#define CHECK(value) do { ++checks; if (!(value)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " + #value); } while (0)

struct Fixture {
    GBContext ctx{};
    GBPPU ppu{};
    std::vector<uint8_t> rom = std::vector<uint8_t>(0x38 * 0x4000);
    std::array<uint8_t, 0x8000> wram{};
    std::array<uint8_t, 0x4000> vram{};
    std::array<uint8_t, 160> oam{};
    std::array<uint8_t, 128> io{};
    std::array<uint8_t, 127> hram{};

    static void word(uint8_t* p, unsigned n) { p[0] = n & 255; p[1] = (n >> 8) & 255; }
    uint8_t& r(unsigned bank, unsigned addr) { return rom[bank * 0x4000 + addr - 0x4000]; }
    void tile_pixel(int tile, int x, int y, unsigned value, int bank = 0) {
        auto* data = vram.data() + bank * 0x2000 + tile * 16 + y * 2;
        const uint8_t bit = uint8_t(0x80 >> x);
        data[0] = uint8_t((data[0] & ~bit) | ((value & 1) ? bit : 0));
        data[1] = uint8_t((data[1] & ~bit) | ((value & 2) ? bit : 0));
    }
    void map_id(int x, int y, unsigned id) {
        r(1, 0x4000 + y * 16 + x) = id & 255;
        r(2, 0x4000 + y * 16 + x) = (id >> 8) & 255;
    }
    void refresh_bg() {
        for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
            const unsigned id = r(1, 0x4000 + (y / 2) * 16 + x / 2);
            const unsigned q = (x & 1) | ((y & 1) << 1);
            vram[0x1800 + y * 32 + x] = r(5, 0x4000 + id + q * 0x100);
            vram[0x3800 + y * 32 + x] = r(5, 0x6000 + id + q * 0x100);
        }
    }
    Fixture() {
        ctx.config.model = GB_MODEL_CGB;
        ctx.rom = rom.data(); ctx.rom_size = rom.size(); ctx.ppu = &ppu;
        ctx.wram = wram.data(); ctx.vram = vram.data(); ctx.oam = oam.data();
        ctx.io = io.data(); ctx.hram = hram.data(); ctx.wram_bank = 1;
        ctx.rom_bank = 8;
        ppu.lcdc = io[0x40] = 0x97;
        ppu.scx = ppu.scy = 48;
        wram[0x131] = 1; wram[0x13a] = 1;
        wram[0x19f] = wram[0x1a0] = 16;
        word(wram.data() + 0x1a1, 0x4000); wram[0x1a3] = 1;
        word(wram.data() + 0x1a4, 0x4000); wram[0x1a6] = 5;
        word(wram.data() + 0x1a7, 0x6000); wram[0x1a9] = 5;
        word(wram.data() + 0x13f, 0x4000); wram[0x141] = 4;
        word(wram.data() + 0x142, 0x7000); wram[0x144] = 0x35;
        word(wram.data() + 0x1c5, 48); word(wram.data() + 0x1c7, 48);
        wram[0x380] = wram[0x382] = 0xd0; wram[0x1000] = 1; wram[0x106e] = 1;
        word(wram.data() + 0x102e, 152 * 16); word(wram.data() + 0x1030, 136 * 16);
        word(wram.data() + 0x1032, 152 * 16); word(wram.data() + 0x1034, 136 * 16);
        for (int row = 0; row < 8; ++row) r(0x35, 0x7008 + row) = 0xff;
        for (int q = 0; q < 4; ++q) for (int y = 0; y < 8; ++y) {
            unsigned bits = 0;
            for (int x = 0; x < 8; ++x) {
                const float dx = x + (q & 1) * 8 - 7.5f, dy = y + (q >> 1) * 8 - 7.5f;
                if (dx * dx + dy * dy <= 49.0f) bits |= 0x80 >> x;
            }
            r(0x35, 0x7000 + (q + 2) * 8 + y) = bits;
        }
        const unsigned rgb[4][4][3] = {
            {{25, 28, 25}, {19, 23, 21}, {14, 18, 17}, {8, 11, 12}},
            {{25, 28, 28}, {16, 21, 21}, {10, 15, 17}, {5, 9, 12}},
            {{29, 23, 15}, {21, 13, 8}, {13, 8, 6}, {6, 5, 6}},
            {{24, 27, 20}, {12, 20, 13}, {7, 12, 10}, {3, 6, 7}}
        };
        for (int p = 0; p < 8; ++p) for (int c = 0; c < 4; ++c) {
            const unsigned* col = rgb[p % 4][c];
            word(ppu.bg_palette_ram + p * 8 + c * 2, col[0] | (col[1] << 5) | (col[2] << 10));
            word(ppu.obj_palette_ram + p * 8 + c * 2, col[0] | (col[1] << 5) | (col[2] << 10));
        }
        for (unsigned id = 0; id < 6; ++id) {
            for (unsigned q = 0; q < 4; ++q) {
                const int tile = int(id * 4 + q);
                r(5, 0x4000 + id + q * 0x100) = uint8_t(tile);
                r(5, 0x6000 + id + q * 0x100) = uint8_t(id == 4 ? 0x80 : id % 4);
                r(4, 0x4000 + id + q * 0x200) = uint8_t(id == 0 || id == 4 ? 0 : id == 3 ? q + 2 : 1);
                r(4, 0x4800 + id + q * 0x200) = id == 5 ? 4 : 0;
                for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
                    const int xx = x + (q & 1) * 8, yy = y + (q >> 1) * 8;
                    unsigned c = 1;
                    if (id == 0 || id == 4 || id == 5) {
                        if (xx == 0 || yy == 0) c = 2;
                        if ((xx == 2 || xx == 13) && (yy == 2 || yy == 13)) c = 3;
                        if (id == 4 && xx > 5 && xx < 10) c = 0;
                    } else if (id == 1) {
                        if (yy <= 1) c = 0;
                        else if (yy == 2 || yy == 14) c = 3;
                        else if (yy == 3 || yy == 13) c = 2;
                        else if (xx == 0 || xx == 15) c = 3;
                        else if (xx == 2) c = 0;
                    } else if (id == 2) {
                        if (xx < 2 || xx > 13 || yy < 2 || yy > 13) c = 3;
                        else if (xx == 2 || yy == 2) c = 0;
                        else if (yy == 8) c = 2;
                    } else {
                        if (xx < 3 || xx > 12) c = 3;
                        else if (xx == 3 || xx == 4) c = 0;
                        if (yy == 3 || yy == 12) c = 2;
                    }
                    tile_pixel(tile, x, y, c);
                }
            }
        }
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) {
            const bool border = x == 0 || y == 0 || x == 15 || y == 15;
            map_id(x, y, border ? 1 : 0);
            r(3, 0x4000 + y * 16 + x) = 1;
        }
        for (int y = 2; y <= 4; ++y) for (int x = 3; x <= 6; ++x) map_id(x, y, 1);
        for (int x = 2; x <= 6; ++x) map_id(x, 10, 1);
        for (int y = 6; y <= 12; y += 2) map_id(12, y, 3);
        map_id(4, 7, 2); map_id(5, 7, 2); map_id(8, 5, 2);
        map_id(8, 8, 4); map_id(7, 10, 5);
        refresh_bg();
    }
};

static Actor make_actor(float x, float y, bool enemy = false) {
    Actor a; a.feet = {x, y, 0}; a.width = 16; a.height = 24; a.left = -8;
    for (int yy = 0; yy < 24; ++yy) for (int xx = 0; xx < 16; ++xx) {
        uint32_t color = 0;
        if (yy < 7 && xx >= 5 && xx <= 10) color = yy < 2 ? 0xff262128 : 0xffc9a680;
        if (yy >= 7 && yy <= 16 && xx >= 3 && xx <= 12) color = enemy ? 0xff758458 : 0xff874737;
        if (yy >= 8 && yy <= 13 && (xx == 2 || xx == 13)) color = 0xffc9a680;
        if (yy >= 17 && yy <= 21 && ((xx >= 4 && xx <= 6) || (xx >= 9 && xx <= 11))) color = 0xff3a424b;
        if (yy >= 22 && ((xx >= 3 && xx <= 6) || (xx >= 9 && xx <= 12))) color = 0xff161d25;
        if (yy == 15 && xx >= 3 && xx <= 12) color = 0xffb29d6b;
        if (yy >= 8 && yy <= 14 && xx == 7) color = 0xff38252a;
        a.pixels[yy * Actor::MaxSize + xx] = color;
    }
    return a;
}

static void ppm(const std::filesystem::path& path, const uint32_t* pixels, int width, int height) {
    FILE* f = std::fopen(path.string().c_str(), "wb");
    CHECK(f != nullptr);
    std::fprintf(f, "P6\n%d %d\n255\n", width, height);
    for (int i = 0; i < width * height; ++i) {
        const uint8_t rgb[3] = {uint8_t(pixels[i] >> 16), uint8_t(pixels[i] >> 8), uint8_t(pixels[i])};
        CHECK(std::fwrite(rgb, 1, 3, f) == 3);
    }
    CHECK(std::fclose(f) == 0);
}

static void test_decoder() {
    Fixture f;
    GaidenMap map;
    CHECK(map.open(&f.ctx));
    CHECK(map.width == 16 && map.height == 16 && map.room == 1);
    bool blocked = true;
    CHECK(map.blocked(24, 24, blocked) && !blocked);
    CHECK(map.blocked(0, 0, blocked) && blocked);
    CHECK(map.blocked(7 * 16 + 3, 10 * 16 + 4, blocked) && !blocked);
    CHECK(map.blocked(12 * 16, 6 * 16, blocked) && !blocked);
    CHECK(map.blocked(12 * 16 + 7, 6 * 16 + 7, blocked) && blocked);
    CHECK(!map.blocked(-1, 0, blocked));
    CHECK(!map.blocked(256, 0, blocked));
    const auto saved_wram = f.wram;
    const auto saved_vram = f.vram;
    const uint16_t old_bank = f.ctx.rom_bank;
    MapCell c;
    for (int q = 0; q < 4; ++q) {
        CHECK(map.cell(8 + (q & 1), 14 + (q >> 1), c));
        CHECK(c.tile == 8 + q);
        CHECK(c.collision[3] == 255);
    }
    CHECK(f.wram == saved_wram && f.vram == saved_vram && f.ctx.rom_bank == old_bank);

    f.map_id(1, 1, 0x101);
    f.r(5, 0x4501) = 23;
    f.r(5, 0x6501) = 2;
    f.r(4, 0x4301) = 1;
    f.r(4, 0x4b01) = 0;
    CHECK(map.cell(3, 2, c) && c.metatile == 0x101 && c.tile == 23 && c.attributes == 2);
    CHECK(c.collision[0] == 255);
    f.map_id(1, 1, 0);

    f.wram[0x19e] = 8;
    f.wram[0x5000 + 17] = 2; f.wram[0x6000 + 17] = 0;
    f.ctx.wram_bank = 2;
    Fixture::word(f.wram.data() + 0x1a1, 0xd000);
    CHECK(map.open(&f.ctx));
    uint16_t id = 0;
    CHECK(map.metatile(1, 1, id) && id == 2);
    CHECK(map.cell(2, 2, c) && c.metatile == 2);
    Fixture::word(f.wram.data() + 0x1a1, 0x4000);
    f.wram[0x19e] = 4;
    f.r(3, 0x4000 + 17) = 2;
    CHECK(map.open(&f.ctx));
    bool visible = true;
    CHECK(map.metatile(1, 1, id, &visible) && !visible);
    f.wram[0x106e] = 2;
    CHECK(map.open(&f.ctx));
    CHECK(map.metatile(1, 1, id, &visible) && visible);

    c = {}; c.visible = true; c.tile = 31; c.attributes = 8;
    for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) f.tile_pixel(31, x, y, 0, 1);
    f.tile_pixel(31, 0, 0, 3, 1);
    const uint32_t dark = color555(little_u16(f.ppu.bg_palette_ram + 6));
    CHECK(map.background_pixel(c, 0, 0) == dark);
    c.attributes |= 0x60;
    CHECK(map.background_pixel(c, 7, 7) == dark);
    f.ppu.lcdc &= ~0x10;
    c.attributes = 0; c.tile = 0;
    f.tile_pixel(256, 0, 0, 3);
    CHECK(map.background_pixel(c, 0, 0) == dark);
    f.ppu.lcdc |= 0x10;
    f.tile_pixel(40, 0, 0, 1); f.tile_pixel(41, 7, 7, 2);
    CHECK(map.object_pixel(41, 0, 0, 0, 16) == color555(little_u16(f.ppu.obj_palette_ram + 2)));
    CHECK(map.object_pixel(40, 0x60, 0, 0, 16) == color555(little_u16(f.ppu.obj_palette_ram + 4)));
    CHECK(map.object_pixel(40, 0, 1, 0, 16) == 0);
    Fixture::word(f.wram.data() + 0x142, 0x7fff);
    CHECK(!map.open(&f.ctx));
}

static void test_profiles() {
    Profiles p;
    CHECK(p.parse("tile * 0x3 rail 16\nrect 1 48 32 64 48 roof_x 48\n"));
    CHECK(p.entries().size() == 2);
    const uint64_t revision = p.revision();
    CHECK(!p.parse("rect 1 4095 0 2 20 wall 28\n"));
    CHECK(p.entries().size() == 2 && p.revision() == revision);
    std::string embedded_nul = "tile * 1";
    embedded_nul.push_back('\0');
    embedded_nul += "trailing prop 10\n";
    CHECK(!p.parse(embedded_nul));
    CHECK(p.entries().size() == 2 && p.revision() == revision);
    CHECK(!p.parse("tile * 0 floor 2"));
    CHECK(!p.parse("tile * 0 wall -20"));
    CHECK(!p.parse("tile * 0x10000 wall 28"));
    CHECK(!p.parse(std::string(300 * 1024, ' ')));
    std::string excess_fields = "tile * 0 wall 28";
    for (int i = 0; i < 50000; ++i) excess_fields += " x";
    CHECK(!p.parse(excess_fields));
    CHECK(p.entries().size() == 2 && p.revision() == revision);
    CHECK(shaped_height(Shape::Flat, 64, 0, 0, 16, 16) == 0);
    CHECK(shaped_height(Shape::StairsN, 16, 0, 0, 16, 16) == 16);
    CHECK(shaped_height(Shape::StairsN, 16, 0, 15, 16, 16) == 1);
    CHECK(shaped_height(Shape::RoofX, 32, 7, 0, 16, 16) == 32);
    CHECK(shaped_height(Shape::RoofX, 32, 0, 0, 16, 16) == shaped_height(Shape::RoofX, 32, 15, 0, 16, 16));
}

static void test_rasterizer() {
    Rasterizer r;
    for (int width : {320, 480}) for (float pitch : {35.0f, 55.0f, 85.0f}) for (float yaw : {-45.0f, 0.0f, 45.0f}) {
        Camera coverage; coverage.pitch = pitch; coverage.yaw = yaw;
        const int height = width == 320 ? 180 : 272;
        CHECK(fit_camera_coverage(coverage, width, height, 158, 64));
        CHECK(coverage.zoom >= 1 && coverage.zoom <= 2);
        const uint32_t color = 0xffaabbee;
        for (float z : {0.0f, 64.0f}) {
            CHECK(r.begin(width, height, coverage));
            r.draw(rectangle({-158, -158, z}, {158, -158, z}, {158, 158, z}, {-158, 158, z},
                {0, 0, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 0}), {&color, 1, 1});
            CHECK(std::all_of(r.pixels(), r.pixels() + width * height, [&](uint32_t pixel) { return pixel == color; }));
        }
    }
    Camera camera; camera.target = {0, 0, 0};
    const uint32_t red = 0xffff0000, blue = 0xff0000ff, transparent = 0;
    const Face far = rectangle({-100, -100, 0}, {100, -100, 0}, {100, 100, 0}, {-100, 100, 0},
        {0, 0, 1}, {0, 0}, {0, 0}, {0, 0}, {0, 0});
    Face near = far;
    for (auto& point : near.points) point.z = 20;
    CHECK(r.begin(320, 180, camera));
    r.draw(near, {&red, 1, 1}); r.draw(far, {&blue, 1, 1});
    const Vec3 center = r.project({0, 0, 20});
    const size_t index = int(center.y) * 320 + int(center.x);
    CHECK(r.pixels()[index] == red);
    const float depth = r.depth_buffer()[index];
    CHECK(r.begin(320, 180, camera));
    r.draw(far, {&blue, 1, 1}); r.draw(near, {&red, 1, 1});
    CHECK(r.pixels()[index] == red && std::abs(r.depth_buffer()[index] - depth) < 1e-6f);
    CHECK(r.begin(320, 180, camera));
    r.draw(near, {&transparent, 1, 1}); r.draw(far, {&blue, 1, 1});
    CHECK(r.pixels()[index] == blue);
    CHECK(!r.begin(100000, 100000, camera));
    camera.yaw = std::numeric_limits<float>::quiet_NaN();
    CHECK(!r.begin(320, 180, camera));
    camera.yaw = 0; camera.distance = 128;
    Face clipped = far;
    clipped.points[0].z = clipped.points[1].z = 256;
    CHECK(r.begin(320, 180, camera));
    r.draw(clipped, {&red, 1, 1});
    for (float z : r.depth_buffer()) CHECK(std::isfinite(z) && z >= 0);
}

static void test_scene(const std::filesystem::path& output) {
    Fixture f; GaidenMap map; Profiles profiles; Scene scene; SceneSettings settings;
    CHECK(map.open(&f.ctx));
    CHECK(scene.update(map, settings, profiles));
    CHECK(scene.elevation(24, 24) == 0);
    CHECK(scene.elevation(132, 132) == 0);
    CHECK(scene.elevation(0, 0) == 28);
    CHECK(scene.elevation(70, 118) == 10);
    CHECK(scene.elevation(112, 164) == 0);
    CHECK(scene.face_count() < 4000 && scene.solid_pixels() > 1000);
    const unsigned first_rebuilds = scene.rebuild_count();
    CHECK(scene.update(map, settings, profiles) && scene.rebuild_count() == first_rebuilds);
    f.ppu.bg_palette_ram[0] ^= 1;
    CHECK(scene.update(map, settings, profiles) && scene.rebuild_count() == first_rebuilds);
    f.ppu.bg_palette_ram[0] ^= 1;
    CHECK(profiles.parse("tile * 1 flat 0\ntile 1 1 auto 28\ntile * 3 flat 0\ntile 1 3 rail 16\n"));
    CHECK(scene.update(map, settings, profiles));
    CHECK(scene.elevation(0, 0) == 28);
    CHECK(scene.elevation(192, 96) == 0 && scene.elevation(199, 103) == 16);
    CHECK(profiles.parse("tile * 5 wall 20\n"));
    CHECK(scene.update(map, settings, profiles) && scene.elevation(112, 164) == 0);
    CHECK(profiles.parse("tile_fill * 5 wall 20\n"));
    CHECK(scene.update(map, settings, profiles) && scene.elevation(112, 164) == 20);
    CHECK(scene.support_elevation(112, 164) == 0 && scene.elevation(112, 159) == 0);
    bool blocked = true;
    CHECK(map.blocked(112, 164, blocked) && !blocked);
    f.map_id(7, 10, 0);
    CHECK(scene.update(map, settings, profiles) && scene.elevation(112, 164) == 0);
    f.map_id(7, 10, 5);
    CHECK(scene.update(map, settings, profiles) && scene.elevation(112, 164) == 20);
    CHECK(profiles.parse("tile_fill * 5 platform 20\n"));
    CHECK(scene.update(map, settings, profiles) && scene.support_elevation(112, 164) == 20);
    CHECK(profiles.parse("tile_fill * 5 wall 20\ntile_fill 1 5 flat 0\n"));
    CHECK(scene.update(map, settings, profiles) && scene.elevation(112, 164) == 0);
    CHECK(scene.support_elevation(112, 164) == 0);
    CHECK(profiles.parse("tile * 3 rail 16\nrect 1 48 32 64 48 roof_x 48\nrect 1 144 176 32 32 stairs_n 16\n"));
    CHECK(scene.update(map, settings, profiles));
    CHECK(scene.elevation(192, 96) == 0);
    CHECK(scene.elevation(199, 103) == 16);
    CHECK(scene.elevation(152, 176) == 16 && scene.elevation(152, 207) == 1);
    CHECK(scene.support_elevation(152, 176) == 16 && scene.support_elevation(152, 207) == 1);
    CHECK(scene.support_elevation(80, 48) == 0 && scene.support_elevation(70, 118) == 0);
    CHECK(scene.elevation(80, 48) > scene.elevation(48, 48));
    const auto saved_wram = f.wram; const auto saved_vram = f.vram;
    Rasterizer raster;
    Camera camera; camera.target = {128, 128, 0}; camera.yaw = -12;
    const Actor player = make_actor(152, 136), enemy = make_actor(124, 208, true);
    CHECK(raster.begin(480, 272, camera));
    raster.set_cutaway(player.feet, float(player.height), true);
    scene.draw(raster); Scene::draw_actor(raster, player, camera.yaw); Scene::draw_actor(raster, enemy, camera.yaw);
    CHECK(raster.triangles > 20 && raster.fragments > 10000);
    CHECK(f.wram == saved_wram && f.vram == saved_vram);
    if (!output.empty()) {
        std::filesystem::create_directories(output);
        ppm(output / "voxel_fixture.ppm", raster.pixels(), raster.width(), raster.height());
        std::vector<uint32_t> heightmap(Scene::Side * Scene::Side);
        for (size_t i = 0; i < heightmap.size(); ++i) {
            const unsigned height = scene.height_field()[i] * 4;
            heightmap[i] = 0xff000000 | (height << 16) | (height << 8) | height;
        }
        ppm(output / "collision_height_fixture.ppm", heightmap.data(), Scene::Side, Scene::Side);
        ppm(output / "map_fixture.ppm", scene.map_image().data(), Scene::Side, Scene::Side);
        for (int yaw : {-35, 0, 35}) {
            camera.yaw = float(yaw);
            CHECK(raster.begin(480, 272, camera));
            raster.set_cutaway(player.feet, float(player.height), true);
            scene.draw(raster); Scene::draw_actor(raster, player, camera.yaw); Scene::draw_actor(raster, enemy, camera.yaw);
            ppm(output / ("voxel_fixture_yaw_" + std::to_string(yaw) + ".ppm"), raster.pixels(), raster.width(), raster.height());
        }
    }
    camera.yaw = -12;
    constexpr int frames = 60;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < frames; ++i) {
        CHECK(scene.update(map, settings, profiles));
        CHECK(raster.begin(480, 272, camera));
        raster.set_cutaway(player.feet, float(player.height), true);
        scene.draw(raster); Scene::draw_actor(raster, player, camera.yaw); Scene::draw_actor(raster, enemy, camera.yaw);
    }
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / frames;
    std::printf("Fixture: %zu faces, %u triangles, %u fragments; 480x272 update+render %.3f ms/frame (desktop)\n",
        scene.face_count(), raster.triangles, raster.fragments, ms);

    // A malformed frame must not permanently poison an otherwise valid mesh cache.
    f.r(2, 0x4000 + 17) = 255;
    CHECK(!scene.update(map, settings, profiles));
    f.r(2, 0x4000 + 17) = 0;
    CHECK(scene.update(map, settings, profiles));
    for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) f.map_id(x, y, 1);
    for (int row = 0; row < 8; ++row) f.r(0x35, 0x7008 + row) = (row & 1) ? 0x55 : 0xaa;
    CHECK(profiles.parse(""));
    f.refresh_bg();
    CHECK(!scene.update(map, settings, profiles));
    CHECK(std::strstr(scene.error(), "budget") != nullptr);
    CHECK(!scene.update(map, settings, profiles));
}

static void test_runtime_capture(const std::filesystem::path& output) {
    Fixture f; GaidenMap map;
    CHECK(map.open(&f.ctx));
    config_set_defaults(&g_app_config);
    CHECK(!g_app_config.voxelizer_enabled);
    voxelizer_sync_config();
    CHECK(!g_voxelizer_capture_enabled);
    g_app_config.voxelizer_enabled = true;
    voxelizer_sync_config();
    CHECK(g_voxelizer_capture_enabled);
    f.ppu.lcdc &= ~4; f.io[0x40] = f.ppu.lcdc;
    const Actor source = make_actor(152, 136);
    for (int row = 0; row < 3; ++row) for (int column = 0; column < 2; ++column) {
        const int slot = row * 2 + column, tile = 40 + slot;
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
            const uint32_t pixel = source.pixels[(row * 8 + y) * Actor::MaxSize + column * 8 + x];
            f.tile_pixel(tile, x, y, pixel ? 1 + ((pixel >> 8) % 3) : 0);
        }
        f.oam[slot * 4] = uint8_t(80 + row * 8);
        f.oam[slot * 4 + 1] = uint8_t(104 + column * 8);
        f.oam[slot * 4 + 2] = uint8_t(tile);
        f.oam[slot * 4 + 3] = 2;
    }
    std::array<uint32_t, 160 * 144> native{};
    auto make_native = [&] {
        for (int y = 0; y < 144; ++y) for (int x = 0; x < 160; ++x) {
            MapCell c;
            CHECK(map.cell((x + 48) / 8, (y + 48) / 8, c));
            native[y * 160 + x] = map.background_pixel(c, x + 48, y + 48);
        }
        for (int slot = 5; slot >= 0; --slot) for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
            const uint32_t color = map.object_pixel(f.oam[slot * 4 + 2], f.oam[slot * 4 + 3], x, y, 8);
            if (color) native[(f.oam[slot * 4] - 16 + y) * 160 + f.oam[slot * 4 + 1] - 8 + x] = color;
        }
    };
    auto generation = [&](bool dma = true) {
        voxelizer_entity_frame_begin(&f.ctx);
        f.ctx.hl = 0xc700;
        voxelizer_entity_capture_begin(&f.ctx, 0xd0);
        std::memcpy(f.wram.data() + 0x700, f.oam.data(), 160);
        f.ctx.hl = 0xc718;
        voxelizer_entity_capture_end(&f.ctx);
        voxelizer_entity_frame_end(&f.ctx);
        if (dma) voxelizer_oam_dma_complete(&f.ctx);
    };
    auto frame = [&](int window_y = 144) {
        for (int y = 0; y < 144; ++y) {
            voxelizer_capture_span(&f.ctx, 0, y, 13, y >= window_y);
            voxelizer_capture_span(&f.ctx, 13, y, 146, y >= window_y);
            voxelizer_capture_span(&f.ctx, 159, y, 1, y >= window_y);
        }
    };
    int width = 160, height = 144;
    make_native(); generation();
    CHECK(!voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    const auto before_wram = f.wram;
    const auto before_vram = f.vram;
    frame();
    const uint32_t* pixels = voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true);
    if (!pixels) std::fprintf(stderr, "Runtime status: %s, agreement %.3f\n", voxelizer_get_stats()->status, voxelizer_get_stats()->frame_agreement);
    CHECK(pixels && width == 480 && height == 272);
    CHECK(voxelizer_get_stats()->actors == 1 && voxelizer_get_stats()->active);
    CHECK(voxelizer_get_stats()->frame_agreement > 0.99f);
    CHECK(f.wram == before_wram && f.vram == before_vram);
    const std::vector<uint32_t> stable(pixels, pixels + width * height);
    Fixture::word(f.wram.data() + 0x1c5, 49);
    Fixture::word(f.wram.data() + 0x1032, 153 * 16);
    generation(false);
    frame();
    pixels = voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true);
    CHECK(pixels && std::equal(stable.begin(), stable.end(), pixels));
    CHECK(little_u16(f.wram.data() + 0x1c5) == 49);
    Fixture::word(f.wram.data() + 0x1c5, 48);
    Fixture::word(f.wram.data() + 0x1032, 152 * 16);
    generation(); frame();
    CHECK(voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    f.wram[0x19f] = 3;
    pixels = voxelizer_render_frame(&f.ctx, native.data(), &width, &height, false);
    CHECK(pixels && std::equal(stable.begin(), stable.end(), pixels));
    f.wram[0x19f] = 16;
    g_app_config.voxelizer_yaw = 25;
    pixels = voxelizer_render_frame(&f.ctx, native.data(), &width, &height, false);
    CHECK(pixels && !std::equal(stable.begin(), stable.end(), pixels));
    for (int y = 120; y < 144; ++y) for (int x = 0; x < 160; ++x) native[y * 160 + x] = 0xffddccbb;
    generation(); frame(120);
    pixels = voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true);
    CHECK(pixels && voxelizer_get_stats()->actors == 1);
    CHECK(pixels[(height - 2) * width + width / 2] == 0xffddccbb);
    CHECK(pixels[(height - 2) * width] == 0xff000000);
    if (!output.empty()) ppm(output / "runtime_capture_fixture.ppm", pixels, width, height);
    frame(0);
    CHECK(!voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    CHECK(!voxelizer_get_stats()->active);
    make_native(); frame();
    CHECK(voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    f.ctx.wram_bank = 2; frame();
    CHECK(!voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    f.ctx.wram_bank = 1;
    f.wram[0x131] = 2; frame();
    CHECK(!voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    f.wram[0x131] = 1;
    voxelizer_reset();
    CHECK(!voxelizer_render_frame(&f.ctx, native.data(), &width, &height, false));
    frame();
    CHECK(!voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    generation(); frame();
    CHECK(voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    g_app_config.voxelizer_quality = 0;
    CHECK(voxelizer_render_frame(&f.ctx, native.data(), &width, &height, false));
    CHECK(width == 320 && height == 180);
    g_app_config.voxelizer_pitch = 999; g_app_config.voxelizer_zoom = -1;
    voxelizer_sync_config();
    CHECK(g_app_config.voxelizer_pitch == 85 && g_app_config.voxelizer_zoom == 100);
    g_app_config.voxelizer_enabled = false;
    voxelizer_sync_config();
    CHECK(!g_voxelizer_capture_enabled && !voxelizer_get_stats()->active);
    CHECK(!voxelizer_render_frame(&f.ctx, native.data(), &width, &height, true));
    voxelizer_shutdown();
}

int main(int argc, char** argv) {
    try {
        test_decoder(); test_profiles(); test_rasterizer();
        test_scene(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path());
        test_runtime_capture(argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path());
        std::printf("PASS: %u checks (synthetic fixtures, no commercial ROM required)\n", checks);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "FAIL: %s\n", e.what());
        return 1;
    }
}
