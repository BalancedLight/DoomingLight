#ifndef DOOM_NOCLIP_H
#define DOOM_NOCLIP_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomNoclipDiagnostic {
    int enabled, tracked, validation, camera_mode, saved, owner, actor, input_mode;
    /* 0=none, 1=entered, 2=returned, 3=no transition,
       100+reason=toggle precheck, 200+reason=toggle result, 300+reason=Y result. */
    int last_transition;
    int input_ready, free_camera, captured;
    unsigned raw_keys, owned_keys;
} DoomNoclipDiagnostic;

/* CBC7-only; called on the owned game-frame route. */
int doom_noclip_start(void *game, void *engine, void *level, uint32_t generation,
                       int supported_profile);
/* Toggle native flight with Doom movement and mouse look. */
int doom_noclip_toggle(void);
/* Active only for a validated, observed Y/IDCLIP transition on the same map. */
int doom_noclip_active(void);
/* Also requires foreground and gameplay input, for reading cheat keys. */
int doom_noclip_input_ready(void);
/* Enable or disable the IDCLIP cheat. */
void doom_noclip_set_cheats_enabled(int enabled);
/* Read-only bounded state for diagnostics; unavailable fields are -1. */
void doom_noclip_diagnostic(DoomNoclipDiagnostic *out);
/* True once after a player is recreated by an observed Y/IDCLIP transition. */
int doom_noclip_take_returned(void);
int doom_noclip_stop(void);

#ifdef __cplusplus
}
#endif
#endif
