#ifndef DOOM_POWERUPS_HOST_H
#define DOOM_POWERUPS_HOST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomPowerupsHost DoomPowerupsHost;
/* The caller must stop and destroy the host while its level is still valid. */
DoomPowerupsHost *doom_powerups_host_create(void *engine_module, void *level, uint32_t generation);
int doom_powerups_host_update(DoomPowerupsHost *host, void *level, uint32_t generation,
                              int light_amp, const float camera_position[3]);
int doom_powerups_host_destroy(DoomPowerupsHost *host, void *level, uint32_t generation);
#ifdef __cplusplus
}
#endif
#endif
