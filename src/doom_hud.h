#ifndef DOOM_HUD_H
#define DOOM_HUD_H
#include "doom_core.h"
#include "doom_wad.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomHud DoomHud;
/* Compose a transparent 320x200 RGBA image. The WAD must outlive the HUD. */
DoomHud *doom_hud_create(const DoomWad *wad);
void doom_hud_destroy(DoomHud *hud);
int doom_hud_render(DoomHud *hud, const DoomPlayer *player, uint8_t *rgba, size_t size);
/* Each transparent layer uses 320 pixels per row. The status and message
 * layers are respectively 32 and 16 rows; the weapon layer is 200 rows. */
int doom_hud_render_layers(DoomHud *hud, const DoomPlayer *player,
                           uint8_t *weapon, size_t weapon_size,
                           uint8_t *status, size_t status_size,
                           uint8_t *message, size_t message_size,
                           const char *message_text);
#ifdef __cplusplus
}
#endif
#endif
