#ifndef DOOM_PLAYER_VOICE_H
#define DOOM_PLAYER_VOICE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomPlayerVoice DoomPlayerVoice;
typedef struct DoomPlayerVoiceObservation {
    uint32_t kind;       /* 1: player sound category; 2: local PlaySound2D */
    uint32_t category;
    uint32_t caller_rva; /* UINT32_MAX when called outside gamedll */
    char cue[64];        /* bounded local-view cue; empty if unavailable */
} DoomPlayerVoiceObservation;
/* The caller must first verify the exact supported Player build. */
DoomPlayerVoice *doom_player_voice_create(void *game_module);
int doom_player_voice_install(DoomPlayerVoice *filter);
void doom_player_voice_set_active(DoomPlayerVoice *filter, int active);
/* One-shot local jump events, capped to avoid unbounded queues. */
unsigned doom_player_voice_drain_jumps(DoomPlayerVoice *filter);
unsigned doom_player_voice_drain_observations(DoomPlayerVoice *filter,
                                              DoomPlayerVoiceObservation *out,
                                              unsigned capacity);
int doom_player_voice_uninstall(DoomPlayerVoice *filter);
void doom_player_voice_destroy(DoomPlayerVoice *filter);
#ifdef __cplusplus
}
#endif
#endif
