#ifndef DOOM_MOTION_OBSERVER_H
#define DOOM_MOTION_OBSERVER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
    /* Private debug-only diagnostics; hook access is limited to a validated local
     * actor and only while its caller-owned game-frame context is active. */
    typedef struct DoomMotionObserver DoomMotionObserver;
    typedef enum DoomMotionSampleKind
    {
        DOOM_MOTION_LOCAL_WANTED,
        DOOM_MOTION_LOCAL_ACCEL,
        DOOM_MOTION_LOCAL_DECEL,
        DOOM_MOTION_WORLD_WANTED,
        DOOM_MOTION_WORLD_ACCEL,
        DOOM_MOTION_WORLD_DECEL,
        DOOM_MOTION_INPUT
    } DoomMotionSampleKind;
    typedef struct DoomMotionSample
    {
        uint64_t qpc;
        uint32_t generation, thread_id, kind, move_state;
        int grounded, parkour, input_action;
        float value[3];
        /* Snapshot supplied at begin(), not a position read at hook time. */
        float frame_position[3];
    } DoomMotionSample;
    /* Caller must first verify the paired CBC7/A18 exact binary SHA-256 hashes. */
    DoomMotionObserver *doom_motion_observer_create(void *game_module, void *engine_module,
                                                    int exact_profile_verified);
    int doom_motion_observer_start(DoomMotionObserver *observer);
    /* This may be called only inside the caller-owned gameplay update. actor and
     * level are borrowed current objects; position is a copied native metres value.
     * Returns zero and captures nothing when object ownership is not verified. */
    int doom_motion_observer_begin(DoomMotionObserver *observer, void *actor, void *level,
                                   uint32_t generation, const float position[3],
                                   uint32_t move_state, int grounded, int parkour);
    void doom_motion_observer_note_input(DoomMotionObserver *observer, int action, float value);
    void doom_motion_observer_end(DoomMotionObserver *observer);
    /* Maximum 256 records. Each captured native call is forwarded unchanged. */
    unsigned doom_motion_observer_drain(DoomMotionObserver *observer, DoomMotionSample *samples,
                                        unsigned capacity, unsigned *dropped);
    int doom_motion_observer_stop(DoomMotionObserver *observer);
    void doom_motion_observer_destroy(DoomMotionObserver *observer);
#ifdef __cplusplus
}
#endif
#endif
