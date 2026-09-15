#include "voxelizer.h"
#include "config_ini.h"
#include "game_state.h"
#include "ppu.h"
#include "voxel/scene.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

using namespace revoxel;

bool g_voxelizer_capture_enabled = false;

namespace {

struct EntityPose {
    float draw_x = 0, draw_y = 0, foot_x = 0, foot_y = 0;
    bool valid = false, screen_space = false;
};

struct ObjectGeneration {
    std::array<int8_t, 40> owners;
    std::array<EntityPose, 16> poses{};
    std::array<uint8_t, 160> shadow{};
    const GBContext* owner = nullptr;
    int room = -1;
    uint64_t sequence = 0;
    ObjectGeneration() { owners.fill(-1); }
};

struct Snapshot {
    std::array<uint8_t, 0x8000> wram{};
    std::array<uint8_t, 0x4000> vram{};
    std::array<uint8_t, 160> oam{};
    std::array<uint8_t, 128> io{};
    std::array<uint8_t, 64> bg_palette{}, obj_palette{};
    std::array<uint8_t, 144> window_left{};
    ObjectGeneration objects;
    const GBContext* owner = nullptr;
    uint8_t* rom = nullptr;
    size_t rom_size = 0;
    void* data_mod = nullptr;
    GBConfig config{};
    uint16_t rom_bank = 0;
    uint8_t wram_bank = 0, vram_bank = 0, lcdc = 0, scx = 0, scy = 0;
    uint64_t sequence = 0;
    bool valid = false, captured_background = false;
};

struct State {
    Snapshot pending, completed, held;
    ObjectGeneration working;
    ObjectGeneration hardware_objects;
    std::array<ObjectGeneration, 4> generations;
    uint64_t generation_sequence = 0, capture_sequence = 0;
    int capture_page = -1, capture_start = -1;
    GBContext view{};
    GBPPU ppu{};
    GaidenMap map;
    Scene scene;
    Profiles profiles;
    Rasterizer raster;
    std::vector<uint32_t> composed;
    std::array<uint32_t, 160 * 144> native{};
    std::array<uint8_t, 160 * 144> overlay{};
    std::array<Actor, 16> actors;
    int actor_count = 0, player_actor = -1;
    uint64_t rendered_sequence = 0, settings_key = 0;
    bool held_native_valid = false;
    VoxelizerStats stats{};
};

std::unique_ptr<State> state;
VoxelizerStats disabled_stats{};

std::string local_path(const char* filename) {
    const std::string config = config_get_default_path();
    const size_t slash = config.find_last_of("/\\");
    return (slash == std::string::npos ? std::string() : config.substr(0, slash + 1)) + filename;
}

void status(const char* text) {
    if (!state) return;
    state->stats.active = false;
    std::snprintf(state->stats.status, sizeof(state->stats.status), "%s", text);
}

void clamp_settings() {
    g_app_config.voxelizer_quality = std::clamp(g_app_config.voxelizer_quality, 0, 1);
    g_app_config.voxelizer_pitch = std::clamp(g_app_config.voxelizer_pitch, 35, 85);
    g_app_config.voxelizer_yaw = std::clamp(g_app_config.voxelizer_yaw, -45, 45);
    g_app_config.voxelizer_zoom = std::clamp(g_app_config.voxelizer_zoom, 100, 200);
    g_app_config.voxelizer_wall_height = std::clamp(g_app_config.voxelizer_wall_height, 4, 64);
    g_app_config.voxelizer_prop_height = std::clamp(g_app_config.voxelizer_prop_height, 2, 32);
}

uint64_t settings_key() {
    uint64_t key = 14695981039346656037ull;
    for (int value : {g_app_config.voxelizer_quality, g_app_config.voxelizer_pitch,
                      g_app_config.voxelizer_yaw, g_app_config.voxelizer_zoom,
                      g_app_config.voxelizer_wall_height, g_app_config.voxelizer_prop_height,
                      int(g_app_config.voxelizer_shadows), int(g_app_config.voxelizer_cutaway)})
        key = (key ^ static_cast<unsigned>(value)) * 1099511628211ull;
    return key ^ state->profiles.revision();
}

bool capture_memory(GBContext* ctx, Snapshot& snapshot) {
    if (!ctx || ctx->config.model != GB_MODEL_CGB || ctx->config.cgb_compatibility_mode ||
        !ctx->wram || !ctx->vram || !ctx->oam || !ctx->io || !ctx->ppu || !ctx->rom) return false;
    const auto* ppu = static_cast<const GBPPU*>(ctx->ppu);
    std::memcpy(snapshot.wram.data(), ctx->wram, snapshot.wram.size());
    std::memcpy(snapshot.vram.data(), ctx->vram, snapshot.vram.size());
    std::memcpy(snapshot.oam.data(), ctx->oam, snapshot.oam.size());
    std::memcpy(snapshot.io.data(), ctx->io, snapshot.io.size());
    std::memcpy(snapshot.bg_palette.data(), ppu->bg_palette_ram, 64);
    std::memcpy(snapshot.obj_palette.data(), ppu->obj_palette_ram, 64);
    snapshot.owner = ctx; snapshot.rom = ctx->rom; snapshot.rom_size = ctx->rom_size;
    snapshot.data_mod = ctx->data_mod_state; snapshot.config = ctx->config;
    snapshot.rom_bank = ctx->rom_bank; snapshot.wram_bank = ctx->wram_bank; snapshot.vram_bank = ctx->vram_bank;
    snapshot.lcdc = ppu->lcdc; snapshot.scx = ppu->scx; snapshot.scy = ppu->scy;
    // The VBlank handler at 00:2b8a latches C1C5/C1C7 into SCX/SCY before OAM DMA.
    for (int axis = 0; axis < 2; ++axis) {
        const int offset = axis ? 0x1c7 : 0x1c5;
        const int raw = little_u16(snapshot.wram.data() + offset);
        int latched = (raw & ~255) | (axis ? snapshot.scy : snapshot.scx);
        if (latched - raw > 128) latched -= 256;
        if (raw - latched > 128) latched += 256;
        if (latched < 0 || latched > 4095) return false;
        snapshot.wram[offset] = uint8_t(latched);
        snapshot.wram[offset + 1] = uint8_t(latched >> 8);
    }
    snapshot.objects = ObjectGeneration();
    const ObjectGeneration& hardware = state->hardware_objects;
    if (hardware.sequence && hardware.owner == ctx &&
        std::memcmp(hardware.shadow.data(), snapshot.oam.data(), 160) == 0) snapshot.objects = hardware;
    return true;
}

void bind_snapshot() {
    State& s = *state;
    Snapshot& f = s.held;
    s.view.rom = f.rom; s.view.rom_size = f.rom_size; s.view.data_mod_state = f.data_mod;
    s.view.wram = f.wram.data(); s.view.vram = f.vram.data(); s.view.oam = f.oam.data(); s.view.io = f.io.data();
    s.view.ppu = &s.ppu; s.view.config = f.config;
    s.view.rom_bank = f.rom_bank; s.view.wram_bank = f.wram_bank; s.view.vram_bank = f.vram_bank;
    s.ppu.lcdc = f.lcdc; s.ppu.scx = f.scx; s.ppu.scy = f.scy;
    std::memcpy(s.ppu.bg_palette_ram, f.bg_palette.data(), 64);
    std::memcpy(s.ppu.obj_palette_ram, f.obj_palette.data(), 64);
}

int unwrap(int coordinate, float anchor) {
    while (coordinate - anchor > 128) coordinate -= 256;
    while (anchor - coordinate > 128) coordinate += 256;
    return coordinate;
}

void collect_actors() {
    State& s = *state;
    const Snapshot& f = s.held;
    const int sprite_height = (f.lcdc & 4) ? 16 : 8;
    s.actor_count = 0; s.player_actor = -1;
    s.overlay.fill(0);
    for (int page = 0; page < 16; ++page) {
        const EntityPose& pose = f.objects.poses[page];
        if (!pose.valid) continue;
        std::array<int, 40> sx{}, sy{};
        int minx = 10000, miny = 10000, maxx = -10000, maxy = -10000;
        for (int i = 0; i < 40; ++i) {
            if (f.objects.owners[i] != page || !f.oam[i * 4]) continue;
            const float cx = pose.screen_space ? 0.0f : float(s.map.camera_x);
            const float cy = pose.screen_space ? 0.0f : float(s.map.camera_y);
            sx[i] = unwrap(int(f.oam[i * 4 + 1]) - 8, pose.draw_x - cx);
            sy[i] = unwrap(int(f.oam[i * 4]) - 16, pose.draw_y - cy);
            if (pose.screen_space) {
                for (int y = 0; y < sprite_height; ++y) for (int x = 0; x < 8; ++x) {
                    const int px = sx[i] + x, py = sy[i] + y;
                    if (px < 0 || py < 0 || px >= 160 || py >= 144) continue;
                    if (s.map.object_pixel(f.oam[i * 4 + 2], f.oam[i * 4 + 3], x, y, sprite_height))
                        s.overlay[py * 160 + px] = 1;
                }
                continue;
            }
            minx = std::min(minx, sx[i]); miny = std::min(miny, sy[i]);
            maxx = std::max(maxx, sx[i] + 8); maxy = std::max(maxy, sy[i] + sprite_height);
        }
        if (pose.screen_space || minx >= maxx || miny >= maxy ||
            maxx - minx > Actor::MaxSize || maxy - miny > Actor::MaxSize) continue;
        Actor& actor = s.actors[s.actor_count];
        actor.pixels.fill(0);
        for (int i = 39; i >= 0; --i) {
            if (f.objects.owners[i] != page || !f.oam[i * 4]) continue;
            for (int y = 0; y < sprite_height; ++y) for (int x = 0; x < 8; ++x) {
                const uint32_t pixel = s.map.object_pixel(f.oam[i * 4 + 2], f.oam[i * 4 + 3], x, y, sprite_height);
                if (pixel) actor.pixels[(sy[i] + y - miny) * Actor::MaxSize + sx[i] + x - minx] = pixel;
            }
        }
        int top = maxy - miny, bottom = -1;
        for (int y = 0; y < maxy - miny; ++y) for (int x = 0; x < maxx - minx; ++x) {
            if (actor.pixels[y * Actor::MaxSize + x]) { top = std::min(top, y); bottom = std::max(bottom, y); }
        }
        if (bottom < top) continue;
        if (top) {
            for (int y = top; y <= bottom; ++y)
                std::memmove(actor.pixels.data() + (y - top) * Actor::MaxSize,
                             actor.pixels.data() + y * Actor::MaxSize, Actor::MaxSize * sizeof(uint32_t));
        }
        actor.width = maxx - minx; actor.height = bottom - top + 1;
        const float visual_lift = std::max(0.0f, pose.foot_y - (miny + bottom + 1 + s.map.camera_y));
        actor.feet = {pose.foot_x, pose.foot_y, visual_lift};
        actor.floor_z = 0;
        actor.left = minx + s.map.camera_x - pose.foot_x;
        if (page + 0xd0 == f.wram[0x380]) s.player_actor = s.actor_count;
        ++s.actor_count;
    }
}

float frame_agreement() {
    State& s = *state;
    const Snapshot& f = s.held;
    int matches = 0, samples = 0;
    const int sprite_height = (f.lcdc & 4) ? 16 : 8;
    for (int y = 4; y < 144; y += 8) for (int x = 4; x < 160; x += 8) {
        if (x >= f.window_left[y]) continue;
        bool sprite = false;
        for (int i = 0; i < 40; ++i) {
            const int sx = int(f.oam[i * 4 + 1]) - 8, sy = int(f.oam[i * 4]) - 16;
            if (x >= sx && x < sx + 8 && y >= sy && y < sy + sprite_height) { sprite = true; break; }
        }
        if (sprite) continue;
        MapCell cell;
        const int wx = x + s.map.camera_x, wy = y + s.map.camera_y;
        if (!s.map.cell(wx / 8, wy / 8, cell) || !cell.visible) continue;
        const uint32_t expected = s.map.background_pixel(cell, wx & 7, wy & 7);
        const uint32_t actual = s.native[y * 160 + x];
        ++samples;
        const int error = std::abs(int((expected >> 16) & 255) - int((actual >> 16) & 255)) +
            std::abs(int((expected >> 8) & 255) - int((actual >> 8) & 255)) +
            std::abs(int(expected & 255) - int(actual & 255));
        matches += error <= 12;
    }
    return samples >= 16 ? float(matches) / samples : 0.0f;
}

void composite_interface() {
    State& s = *state;
    const int width = s.raster.width(), height = s.raster.height();
    s.composed.assign(s.raster.pixels(), s.raster.pixels() + size_t(width) * height);
    const int native_width = height * 160 / 144;
    const int left = (width - native_width) / 2;
    for (int y = 0; y < height; ++y) {
        const int ny = std::min(143, y * 144 / height);
        const int window = s.held.window_left[ny];
        for (int x = 0; x < width; ++x) {
            if (x < left || x >= left + native_width) {
                if (window == 0) s.composed[y * width + x] = 0xff000000u;
                continue;
            }
            const int nx = std::clamp((x - left) * 160 / native_width, 0, 159);
            if (nx >= window || s.overlay[ny * 160 + nx]) s.composed[y * width + x] = s.native[ny * 160 + nx];
        }
    }
}

bool write_ppm(const std::string& path, const uint32_t* pixels, int width, int height) {
    FILE* file = std::fopen(path.c_str(), "wb");
    if (!file) return false;
    bool valid = std::fprintf(file, "P6\n%d %d\n255\n", width, height) > 0;
    for (int i = 0; valid && i < width * height; ++i) {
        const uint8_t rgb[] = {uint8_t(pixels[i] >> 16), uint8_t(pixels[i] >> 8), uint8_t(pixels[i])};
        valid = std::fwrite(rgb, 1, 3, file) == 3;
    }
    const bool closed = std::fclose(file) == 0;
    return valid && closed;
}

}

void voxelizer_sync_config(void) {
    clamp_settings();
    if (!g_app_config.voxelizer_enabled) {
        voxelizer_shutdown();
        return;
    }
    if (!state) {
        try { state = std::make_unique<State>(); }
        catch (const std::bad_alloc&) {
            std::snprintf(disabled_stats.status, sizeof(disabled_stats.status), "Voxelizer allocation failed");
            g_voxelizer_capture_enabled = false;
            return;
        }
        status("Waiting for a complete exploration frame");
        voxelizer_reload_profiles();
    }
    g_voxelizer_capture_enabled = true;
}

void voxelizer_reset(void) {
    if (!state) return;
    state->pending.valid = state->completed.valid = state->held.valid = false;
    state->working = ObjectGeneration();
    state->hardware_objects = ObjectGeneration();
    for (auto& generation : state->generations) generation = ObjectGeneration();
    state->capture_page = state->capture_start = -1;
    state->held_native_valid = false;
    state->rendered_sequence = state->settings_key = 0;
    state->scene.invalidate();
    status("Waiting for a complete exploration frame");
}

void voxelizer_shutdown(void) {
    g_voxelizer_capture_enabled = false;
    state.reset();
    disabled_stats = {};
    std::snprintf(disabled_stats.status, sizeof(disabled_stats.status), "Disabled");
}

void voxelizer_capture_span(GBContext* ctx, int x, int y, int length, bool window) {
    if (!state || !g_voxelizer_capture_enabled || x < 0 || y < 0 || y >= 144 || length < 1 || x + length > 160) return;
    Snapshot& pending = state->pending;
    if (x == 0 && y == 0) {
        pending.valid = capture_memory(ctx, pending);
        pending.captured_background = false;
        pending.window_left.fill(160);
    }
    if (!pending.valid || pending.owner != ctx) return;
    if (!window && !pending.captured_background) {
        pending.valid = capture_memory(ctx, pending);
        pending.captured_background = true;
    }
    if (window) pending.window_left[y] = std::min<int>(pending.window_left[y], x);
    if (y == 143 && x + length == 160 && pending.valid) {
        pending.sequence = ++state->capture_sequence;
        state->completed = pending;
        pending.valid = false;
    }
}

void voxelizer_entity_frame_begin(GBContext* ctx) {
    if (!state || !g_voxelizer_capture_enabled) return;
    state->working = ObjectGeneration();
    state->working.owner = ctx;
    state->working.room = ctx && ctx->wram ? ctx->wram[0x131] : -1;
    state->capture_page = state->capture_start = -1;
}

void voxelizer_entity_capture_begin(GBContext* ctx, uint8_t page) {
    if (state) state->capture_page = state->capture_start = -1;
    if (!state || !g_voxelizer_capture_enabled || !ctx || !ctx->wram ||
        ctx->config.model != GB_MODEL_CGB || page < 0xd0 || page > 0xdf ||
        ctx->hl < 0xc700 || ctx->hl >= 0xc7a0 || (ctx->hl & 3)) return;
    if (state->working.owner != ctx) voxelizer_entity_frame_begin(ctx);
    state->capture_page = page - 0xd0;
    state->capture_start = (ctx->hl - 0xc700) / 4;
    int bank = ctx->wram[0x13a] & 7;
    if (!bank) bank = 1;
    const uint8_t* entity = ctx->wram + bank * 0x1000 + (page - 0xd0) * 0x100;
    EntityPose& pose = state->working.poses[page - 0xd0];
    pose.draw_x = little_u16(entity + 0x2e) / 16.0f;
    pose.draw_y = (int(little_u16(entity + 0x30)) + int(static_cast<int16_t>(little_u16(entity + 0x2a)))) / 16.0f;
    pose.foot_x = little_u16(entity + 0x32) / 16.0f;
    pose.foot_y = little_u16(entity + 0x34) / 16.0f;
    pose.screen_space = (entity[9] & 8) != 0;
    pose.valid = true;
}

void voxelizer_entity_capture_end(GBContext* ctx) {
    if (!state || !g_voxelizer_capture_enabled || !ctx || state->capture_page < 0) return;
    if (ctx == state->working.owner && ctx->hl >= 0xc700 && ctx->hl <= 0xc7a0 && !(ctx->hl & 3)) {
        const int end = (ctx->hl - 0xc700) / 4;
        if (end >= state->capture_start)
            for (int i = state->capture_start; i < end; ++i) state->working.owners[i] = int8_t(state->capture_page);
    }
    state->capture_page = state->capture_start = -1;
}

void voxelizer_entity_frame_end(GBContext* ctx) {
    if (!state || !g_voxelizer_capture_enabled || !ctx || !ctx->wram || state->working.owner != ctx) return;
    std::memcpy(state->working.shadow.data(), ctx->wram + 0x700, 160);
    state->working.sequence = ++state->generation_sequence;
    state->generations[state->generation_sequence % state->generations.size()] = state->working;
}

void voxelizer_oam_dma_complete(GBContext* ctx) {
    if (!state || !g_voxelizer_capture_enabled || !ctx || !ctx->oam) return;
    const ObjectGeneration* best = nullptr;
    for (const ObjectGeneration& generation : state->generations) {
        if (!generation.sequence || generation.owner != ctx ||
            std::memcmp(generation.shadow.data(), ctx->oam, 160) != 0) continue;
        if (!best || best->sequence < generation.sequence) best = &generation;
    }
    // Identical OAM bytes can describe different world poses; bind ownership at DMA time, not later during presentation.
    state->hardware_objects = best ? *best : ObjectGeneration();
}

const uint32_t* voxelizer_render_frame(GBContext* ctx, const uint32_t* native_frame,
                                       int* width, int* height, bool completed_guest_frame) {
    if (!state || !g_voxelizer_capture_enabled || !ctx || !native_frame || !width || !height) return nullptr;
    State& s = *state;
    if (completed_guest_frame) {
        if (!s.completed.valid || s.completed.owner != ctx || s.completed.rom != ctx->rom ||
            s.completed.data_mod != ctx->data_mod_state) { status("Waiting for a complete frame"); return nullptr; }
        s.held = s.completed;
        std::memcpy(s.native.data(), native_frame, sizeof(s.native));
        s.held_native_valid = true;
    }
    if (!s.held.valid || !s.held_native_valid || s.held.owner != ctx || s.held.rom != ctx->rom ||
        s.held.data_mod != ctx->data_mod_state) { status("Waiting for a complete frame"); return nullptr; }
    clamp_settings();
    const uint64_t key = settings_key();
    if (s.rendered_sequence == s.held.sequence && s.settings_key == key) {
        if (!s.stats.active) return nullptr;
        *width = s.stats.width; *height = s.stats.height;
        return s.composed.data();
    }
    s.rendered_sequence = s.held.sequence; s.settings_key = key;
    s.stats.active = false; s.stats.actors = 0; s.stats.frame_agreement = 0;
    bind_snapshot();
    if (gb_state_is_ui_screen(&s.view) || (s.held.lcdc & 0x82) != 0x82) {
        status("Native 2D: menu, transition or non-exploration screen"); return nullptr;
    }
    if (!s.held.captured_background || !s.map.open(&s.view)) {
        status("Native 2D: no valid world map"); return nullptr;
    }
    s.stats.room = s.map.room;
    if (!s.held.objects.sequence || s.held.objects.room != s.map.room) {
        status("Waiting for matching metasprite DMA capture"); return nullptr;
    }
    collect_actors();
    if (s.player_actor < 0) { status("Native 2D: no exploration player metasprite"); return nullptr; }
    s.stats.frame_agreement = frame_agreement();
    if (s.stats.frame_agreement < 0.50f) {
        status("Native 2D: current picture does not match the world map"); return nullptr;
    }
    const auto start = std::chrono::steady_clock::now();
    try {
        const SceneSettings settings{g_app_config.voxelizer_wall_height, g_app_config.voxelizer_prop_height,
                                     g_app_config.voxelizer_shadows};
        if (!s.scene.update(s.map, settings, s.profiles)) { status(s.scene.error()); return nullptr; }
        for (int i = 0; i < s.actor_count; ++i) {
            Actor& actor = s.actors[i];
            bool blocked = true;
            const int x = int(actor.feet.x), y = int(actor.feet.y);
            if (s.map.blocked(x, y, blocked) && !blocked) actor.floor_z = float(s.scene.support_elevation(x, y));
            actor.feet.z += actor.floor_z;
        }
        Camera camera;
        camera.target = {float(s.map.camera_x + 80), float(s.map.camera_y + 72), 0};
        camera.pitch = float(g_app_config.voxelizer_pitch); camera.yaw = float(g_app_config.voxelizer_yaw);
        camera.zoom = g_app_config.voxelizer_zoom / 100.0f;
        const int rw = g_app_config.voxelizer_quality ? 480 : 320;
        const int rh = g_app_config.voxelizer_quality ? 272 : 180;
        // Keep the finite world patch outside the view even at shallow, diagonal camera angles.
        if (!fit_camera_coverage(camera, rw, rh, Scene::Side * 0.5f - 34, 64)) {
            status("Native 2D: camera exceeds world coverage"); return nullptr;
        }
        if (!s.raster.begin(rw, rh, camera)) { status("Voxelizer framebuffer allocation failed"); return nullptr; }
        const Actor& player = s.actors[s.player_actor];
        s.raster.set_cutaway(player.feet, float(player.height), g_app_config.voxelizer_cutaway);
        s.scene.draw(s.raster);
        for (int i = 0; i < s.actor_count; ++i)
            Scene::draw_actor(s.raster, s.actors[i], camera.yaw, g_app_config.voxelizer_shadows);
        composite_interface();
        s.stats.active = true; s.stats.width = rw; s.stats.height = rh;
        s.stats.actors = s.actor_count; s.stats.faces = unsigned(s.scene.face_count());
        s.stats.triangles = s.raster.triangles; s.stats.fragments = s.raster.fragments;
        s.stats.rebuilds = s.scene.rebuild_count();
        s.stats.compose_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        std::snprintf(s.stats.status, sizeof(s.stats.status), "3D exploration | room %02X | effective zoom %.0f%%", s.map.room, camera.zoom * 100);
        *width = rw; *height = rh;
        return s.composed.data();
    } catch (const std::bad_alloc&) {
        status("Voxelizer allocation failed; native 2D retained");
        return nullptr;
    }
}

const VoxelizerStats* voxelizer_get_stats(void) {
    return state ? &state->stats : &disabled_stats;
}

bool voxelizer_reload_profiles(void) {
    if (!state) return false;
    try {
        const bool result = state->profiles.load(local_path("voxel_profiles.txt").c_str());
        std::snprintf(state->stats.profile_status, sizeof(state->stats.profile_status), "%s", state->profiles.message().c_str());
        state->settings_key = 0;
        return result;
    } catch (const std::bad_alloc&) {
        std::snprintf(state->stats.profile_status, sizeof(state->stats.profile_status), "Profile allocation failed; previous rules retained");
        return false;
    }
}

bool voxelizer_dump_scene(void) {
    if (!state || !state->stats.active) return false;
    State& s = *state;
    char stem[64];
    std::snprintf(stem, sizeof(stem), "voxel_room_%02X", s.map.room);
    const std::string prefix = local_path(stem);
    bool ok = write_ppm(prefix + "_view.ppm", s.composed.data(), s.stats.width, s.stats.height) &&
        write_ppm(prefix + "_map.ppm", s.scene.map_image().data(), Scene::Side, Scene::Side);
    FILE* csv = std::fopen((prefix + "_cells.csv").c_str(), "w");
    if (!csv) return false;
    ok &= std::fprintf(csv, "room,x,y,metatile,tile,attributes,collision_type,mask0,mask1,mask2,mask3,mask4,mask5,mask6,mask7,visible\n") > 0;
    for (int y = s.scene.origin_y() / 8; y < (s.scene.origin_y() + Scene::Side) / 8; ++y) {
        for (int x = s.scene.origin_x() / 8; x < (s.scene.origin_x() + Scene::Side) / 8; ++x) {
            MapCell c;
            if (!s.map.cell(x, y, c)) continue;
            ok &= std::fprintf(csv, "%d,%d,%d,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%d\n", s.map.room, x * 8, y * 8,
                c.metatile, c.tile, c.attributes, c.collision_type, c.collision[0], c.collision[1], c.collision[2], c.collision[3],
                c.collision[4], c.collision[5], c.collision[6], c.collision[7], int(c.visible)) > 0;
        }
    }
    const bool closed = std::fclose(csv) == 0;
    return ok && closed;
}
