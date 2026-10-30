#ifndef DOOM_WORLD_SPRITES_H
#define DOOM_WORLD_SPRITES_H
#include "doom_world.h"
#ifdef __cplusplus
extern "C"
{
#endif
    typedef enum DoomSpriteKind
    {
        DOOM_SPRITE_PICKUP,
        DOOM_SPRITE_PROJECTILE,
        DOOM_SPRITE_BFG_HIT,
        DOOM_SPRITE_HIT
    } DoomSpriteKind;
    /* Position is in Doom units. The host renderer owns projection, depth testing,
       WAD image lifetime and drawing; the name pointer is static. */
    typedef int (*DoomSpriteSink)(void *context, DoomSpriteKind kind, const char *lump,
                                  DoomVec3 position);
    unsigned doom_world_render_sprites(const DoomWorld *world, DoomSpriteSink sink, void *context);
#ifdef __cplusplus
}
#endif
#endif
