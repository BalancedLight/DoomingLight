#ifndef DOOM_DAMAGE_DIRECTION_H
#define DOOM_DAMAGE_DIRECTION_H
#include "doom_core.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomDamageDirection DoomDamageDirection;
/* A successful view acquisition holds the caller's engine lock until release. */
typedef int (*DoomDamageViewFn)(void *context, void **player, void **camera);
typedef void (*DoomDamageViewReleaseFn)(void *context);
typedef struct DoomDamageDirectionSnapshot {
    unsigned hook_calls, local_calls, side_reads, matched_reads;
    int last_reason, last_side;
} DoomDamageDirectionSnapshot;
enum DoomDamageDirectionReason {
    DOOM_DIRECTION_NONE, DOOM_DIRECTION_NO_INFO, DOOM_DIRECTION_NO_VIEW,
    DOOM_DIRECTION_OTHER_RECEIVER, DOOM_DIRECTION_NO_SOURCE, DOOM_DIRECTION_SELF,
    DOOM_DIRECTION_NOT_ACTOR, DOOM_DIRECTION_WRONG_LEVEL, DOOM_DIRECTION_STALE_ID,
    DOOM_DIRECTION_BAD_VECTORS, DOOM_DIRECTION_DEGENERATE,
    DOOM_DIRECTION_FRONT, DOOM_DIRECTION_LEFT, DOOM_DIRECTION_RIGHT, DOOM_DIRECTION_EXCEPTION
};
DoomDamageDirection *doom_damage_direction_start(void *game, void *engine, DoomDamageViewFn view,
                                                DoomDamageViewReleaseFn release, void *context);
int doom_damage_direction_side(const DoomDamageDirection *direction, const void *player);
DoomEnvironmentDamageKind doom_damage_direction_environment(const DoomDamageDirection *direction, const void *player);
int doom_damage_direction_snapshot(const DoomDamageDirection *direction, DoomDamageDirectionSnapshot *snapshot);
int doom_damage_direction_stop(DoomDamageDirection *direction);
#ifdef __cplusplus
}
#endif
#endif
