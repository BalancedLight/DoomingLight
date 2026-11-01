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
/* Apply after composing the weapon or reaching hand; status text stays intact. */
void doom_hud_powerup_visuals(const DoomHud *hud, const DoomPlayer *player,
                              uint8_t *rgba, size_t size);
/* Call once per 35 Hz gameplay tic with horizontal speed in metres/second.
 * Reset when the map, player, or movement sample changes. */
void doom_hud_bob_tick(DoomHud *hud, float horizontal_speed_mps);
void doom_hud_bob_reset(DoomHud *hud);
/* Update once per rendered frame. lower_pixels is the requested parkour dip
 * in 320x200 logical pixels, clamped to 0..200. Outputs are subpixel offsets
 * to be applied to the weapon layer's presentation transform. */
void doom_hud_motion_update(DoomHud *hud, const DoomPlayer *player, unsigned elapsed_ms,
                            float horizontal_speed_mps, float lower_pixels,
                            float *offset_x, float *offset_y);
void doom_hud_motion_reset(DoomHud *hud);
float doom_hud_lowering(const DoomHud *hud);
int doom_hud_pickup_gesture(const DoomHud *hud);
int doom_hud_render(DoomHud *hud, const DoomPlayer *player, uint8_t *rgba, size_t size);
typedef enum DoomHudHandResult {
    DOOM_HUD_HAND_WEAPON = 0,
    DOOM_HUD_HAND_VISIBLE = 1,
    DOOM_HUD_HAND_HIDDEN = 2
} DoomHudHandResult;
/* Update once per rendered frame. A nonzero pickup pulse starts a short hand
 * gesture. force_hidden keeps the weapon hidden after the gesture has ended.
 * The returned hand position is a logical-pixel vertical layer offset. */
DoomHudHandResult doom_hud_hand_update(DoomHud *hud, unsigned elapsed_ms,
                                       int reach_side, int pickup_pulse,
                                       int interaction_active, int force_hidden,
                                       int *side, float *offset_y);
/* Replace the weapon layer with the IWAD hand. side is -1 (left) or 1 (right). */
int doom_hud_render_reach(DoomHud *hud, uint8_t *weapon, size_t size, int side);
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
