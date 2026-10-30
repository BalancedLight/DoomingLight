#ifndef DOOM_PLAYER_VOICE_H
#define DOOM_PLAYER_VOICE_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomPlayerVoice DoomPlayerVoice;
/* The caller must first verify the exact supported Player build. */
DoomPlayerVoice *doom_player_voice_create(void *game_module);
int doom_player_voice_install(DoomPlayerVoice *filter);
void doom_player_voice_set_active(DoomPlayerVoice *filter, int active);
int doom_player_voice_uninstall(DoomPlayerVoice *filter);
void doom_player_voice_destroy(DoomPlayerVoice *filter);
#ifdef __cplusplus
}
#endif
#endif
