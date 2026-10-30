#ifndef DOOM_HIT_RESOLVER_H
#define DOOM_HIT_RESOLVER_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomHitResolver DoomHitResolver;
typedef enum DoomHitResolution {
    DOOM_HIT_UNAVAILABLE = 0,
    DOOM_HIT_MISS = 1,
    DOOM_HIT_RESOLVED = 2
} DoomHitResolution;
typedef enum DoomHitReason {
    DOOM_HIT_REASON_NONE = 0,
    DOOM_HIT_REASON_ARGUMENT,
    DOOM_HIT_REASON_SEGMENT,
    DOOM_HIT_REASON_ACTOR,
    DOOM_HIT_REASON_VISUAL_GETTER,
    DOOM_HIT_REASON_VISUAL_MODEL,
    DOOM_HIT_REASON_MODEL_MISS,
    DOOM_HIT_REASON_SOURCE_MESH,
    DOOM_HIT_REASON_BONE,
    DOOM_HIT_REASON_VISUAL_MESH,
    DOOM_HIT_REASON_CHANGED,
    DOOM_HIT_REASON_OFF_SEGMENT,
    DOOM_HIT_REASON_EXCEPTION
} DoomHitReason;
typedef struct DoomResolvedHit {
    int mesh_element;
    unsigned bone;
    float point[3];
    DoomHitReason reason;
} DoomResolvedHit;
/* The caller validates the exact build, target identity, level and update thread. */
DoomHitResolver *doom_hit_resolver_create(void *game_module, void *engine_module);
DoomHitResolution doom_hit_resolver_trace(DoomHitResolver *resolver, void *actor,
                                           const float start[3], const float end[3],
                                           DoomResolvedHit *hit);
void doom_hit_resolver_destroy(DoomHitResolver *resolver);
#ifdef __cplusplus
}
#endif
#endif
