#ifndef DOOM_INTERACTION_PRESENTATION_H
#define DOOM_INTERACTION_PRESENTATION_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomInteractionPresentation DoomInteractionPresentation;

/* game_module must already match one of Doom Mode's exact Player profiles. */
DoomInteractionPresentation *doom_interaction_presentation_create(void *game_module);
int doom_interaction_presentation_install(DoomInteractionPresentation *presentation);
void doom_interaction_presentation_set_active(DoomInteractionPresentation *presentation, int active);
int doom_interaction_presentation_uninstall(DoomInteractionPresentation *presentation);
void doom_interaction_presentation_destroy(DoomInteractionPresentation *presentation);

#ifdef __cplusplus
}
#endif
#endif
