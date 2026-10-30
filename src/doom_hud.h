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
#ifdef __cplusplus
}
#endif
#endif
