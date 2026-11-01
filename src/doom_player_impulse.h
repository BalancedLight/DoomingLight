#pragma once

/* Call on the game thread with the current player and its current level.
 * The caller must validate the CBC7/A18 profile and exclude vehicles and
 * traversal states before calling. Direction uses Dying Light world axes. */
extern "C" int doom_player_impulse(void *player, void *level, void *engine,
                                   float direction_x, float direction_z, float speed);
