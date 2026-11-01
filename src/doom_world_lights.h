#ifndef DOOM_WORLD_LIGHTS_H
#define DOOM_WORLD_LIGHTS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomWorldLights DoomWorldLights;
typedef enum DoomWorldLightKind {
    DOOM_WORLD_LIGHT_PLASMA,
    DOOM_WORLD_LIGHT_BFG,
    DOOM_WORLD_LIGHT_ROCKET_FLASH,
    DOOM_WORLD_LIGHT_AMP,
    DOOM_WORLD_LIGHT_PLASMA_FLASH,
    DOOM_WORLD_LIGHT_BFG_FLASH,
    DOOM_WORLD_LIGHT_BFG_TRACER
} DoomWorldLightKind;
DoomWorldLights *doom_world_lights_create(void *module, void *level, uint32_t generation);
uint32_t doom_world_lights_add(DoomWorldLights *lights, void *level, uint32_t generation,
                               const float position[3], int kind);
int doom_world_lights_move(DoomWorldLights *lights, void *level, uint32_t generation,
                           uint32_t token, const float position[3]);
int doom_world_lights_remove(DoomWorldLights *lights, void *level, uint32_t generation, uint32_t token);
int doom_world_lights_destroy(DoomWorldLights *lights, void *level, uint32_t generation);
#ifdef __cplusplus
}
#endif
#endif
