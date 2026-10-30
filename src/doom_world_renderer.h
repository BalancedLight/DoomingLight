#ifndef DOOM_MODE_WORLD_RENDERER_H
#define DOOM_MODE_WORLD_RENDERER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomWorldRenderer DoomWorldRenderer;
/* Lab only. Pass the validated module, live level and map generation.
 * All calls must run on the engine game thread. */
DoomWorldRenderer *doom_world_renderer_create(void *engine_module, void *level, uint32_t generation);
/* rgba is an edge-by-edge padded image. Limits: 512 pixels and 64 slots. */
uint32_t doom_world_renderer_add(DoomWorldRenderer *, void *current_level, uint32_t generation, const uint8_t *rgba,
                                 unsigned edge, const float position[3], float size);
int doom_world_renderer_update(DoomWorldRenderer *, void *current_level, uint32_t generation,
                               uint32_t token, const uint8_t *rgba, unsigned edge);
int doom_world_renderer_move(DoomWorldRenderer *, void *current_level, uint32_t generation, uint32_t token, const float position[3]);
int doom_world_renderer_remove(DoomWorldRenderer *, void *current_level, uint32_t generation, uint32_t token);
/* Failure retains ownership; cleanup must happen while the original level is live. */
int doom_world_renderer_destroy(DoomWorldRenderer *, void *current_level, uint32_t generation);
#ifdef __cplusplus
}
#endif
#endif
