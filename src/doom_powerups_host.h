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
/* Weapon is a DoomWeapon value. Melee weapons do not emit muzzle light. */
int doom_powerups_host_muzzle_pulse(DoomPowerupsHost *host, void *level, uint32_t generation,
                                    int weapon, const float camera_position[3], const float forward[3]);
/* Disable clears active pulses; a camera position is required only while enabled. */
int doom_powerups_host_muzzle_tick(DoomPowerupsHost *host, void *level, uint32_t generation,
                                   int enabled, const float camera_position[3]);
int doom_powerups_host_destroy(DoomPowerupsHost *host, void *level, uint32_t generation);
#ifdef __cplusplus
}
#endif
#endif
