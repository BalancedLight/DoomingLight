#pragma once

struct DoomDecals;

/* Use only after validating the A18 engine profile. All calls are game-thread
 * only. The owner releases this object before unloading its engine module. */
extern "C" DoomDecals *doom_decals_open(void *engine);
extern "C" int doom_decals_add(DoomDecals *decals, void *level,
                               const float position[3], const float normal[3]);
extern "C" void doom_decals_close(DoomDecals *decals);
