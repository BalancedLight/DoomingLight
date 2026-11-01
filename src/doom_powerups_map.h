#ifndef DOOM_POWERUPS_MAP_H
#define DOOM_POWERUPS_MAP_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomPowerupsMap DoomPowerupsMap;
/* Call only after the engine has verified the supported Player modules. */
DoomPowerupsMap *doom_powerups_map_create(void *game_module, void *engine_module);
int doom_powerups_map_start(DoomPowerupsMap *map);
void doom_powerups_map_set_active(DoomPowerupsMap *map, int active);
int doom_powerups_map_stop(DoomPowerupsMap *map);
void doom_powerups_map_destroy(DoomPowerupsMap *map);
#ifdef __cplusplus
}
#endif
#endif
