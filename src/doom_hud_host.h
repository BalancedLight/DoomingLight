#ifndef DOOM_HUD_HOST_H
#define DOOM_HUD_HOST_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomHudHost DoomHudHost;
/* level and image are borrowed game-thread objects, validated by the caller.
 * This interface is internal to the native mod, not a DLUMA SDK service. */
DoomHudHost *doom_hud_host_create(void *level, void *image, uint32_t generation);
/* Experimental screen registration; call only on the game thread after the
 * approved companion archive is mounted by the loader. */
DoomHudHost *doom_hud_host_open(void *level, uint32_t generation);
/* Cache the preferred sampling mode; it is applied during the next update. */
void doom_hud_host_set_filtering(DoomHudHost *host, int bilinear);
int doom_hud_host_update(DoomHudHost *host, void *level, void *image,
                         uint32_t generation, const uint8_t *rgba, float scale);
int doom_hud_host_update_layers(DoomHudHost *host, void *level, uint32_t generation,
                                const uint8_t *weapon, const uint8_t *status,
                                const uint8_t *message, float hud_scale, float weapon_scale,
                                float weapon_x, float weapon_y);
void doom_hud_host_destroy(DoomHudHost *host, void *current_level, uint32_t generation);
void doom_hud_host_hide(DoomHudHost *host, void *current_level, uint32_t generation);
/* Hide only native death-screen text while allowing its fade and callbacks. */
void doom_hud_host_death_screen(DoomHudHost *host, void *current_level, uint32_t generation, int suppress);
#ifdef __cplusplus
}
#endif
#endif
