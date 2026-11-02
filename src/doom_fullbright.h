#ifndef DOOM_FULLBRIGHT_H
#define DOOM_FULLBRIGHT_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomFullbright DoomFullbright;

/* Calls require the caller's validated, serialized Player level scope. */
DoomFullbright *doom_fullbright_create(void *game_module, void *engine_module,
                                       void *level, uint32_t generation);
int doom_fullbright_update(DoomFullbright *host, void *level, uint32_t generation, int enabled);
/* A failed restore retains the host so cleanup can be retried with a live level. */
int doom_fullbright_destroy(DoomFullbright *host, void *level, uint32_t generation);
/* Release metadata only at or after confirmed level destruction, when restoration is no longer possible. */
void doom_fullbright_forget(DoomFullbright *host);

#ifdef __cplusplus
}
#endif
#endif
