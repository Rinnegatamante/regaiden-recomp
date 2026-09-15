#ifndef RE_VOXELIZER_H
#define RE_VOXELIZER_H

#include "gbrt.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct VoxelizerStats {
    bool active;
    int width, height, room, actors;
    unsigned faces, triangles, fragments, rebuilds;
    double compose_ms;
    float frame_agreement;
    char status[160];
    char profile_status[160];
} VoxelizerStats;

extern bool g_voxelizer_capture_enabled;
void voxelizer_sync_config(void);
void voxelizer_reset(void);
void voxelizer_shutdown(void);
void voxelizer_capture_span(GBContext* ctx, int x, int y, int length, bool window);
void voxelizer_entity_frame_begin(GBContext* ctx);
void voxelizer_entity_capture_begin(GBContext* ctx, uint8_t page);
void voxelizer_entity_capture_end(GBContext* ctx);
void voxelizer_entity_frame_end(GBContext* ctx);
void voxelizer_oam_dma_complete(GBContext* ctx);
const uint32_t* voxelizer_render_frame(GBContext* ctx, const uint32_t* native_frame,
                                       int* width, int* height, bool completed_guest_frame);
const VoxelizerStats* voxelizer_get_stats(void);
bool voxelizer_reload_profiles(void);
bool voxelizer_dump_scene(void);

#ifdef __cplusplus
}
#endif
#endif
