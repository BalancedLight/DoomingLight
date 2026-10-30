#include "doom_world_sprites.h"
unsigned doom_world_render_sprites(const DoomWorld *world, DoomSpriteSink sink, void *context)
{
    unsigned i, count = 0;
    if (!world || !sink)
        return 0;
    for (i = 0; i < DOOM_WORLD_MAX_PROJECTILES; i++)
    {
        const DoomProjectile *projectile = &world->projectiles[i];
        const char *name = doom_world_projectile_sprite(projectile);
        if (name && sink(context, DOOM_SPRITE_PROJECTILE, name, projectile->position))
            ++count;
    }
    for (i = 0; i < DOOM_WORLD_MAX_BFG_HIT_FX; i++)
    {
        const DoomBfgHitFx *effect = &world->bfg_hit_fx[i];
        const char *name = doom_world_bfg_hit_sprite(effect);
        if (name && sink(context, DOOM_SPRITE_BFG_HIT, name, effect->position))
            ++count;
    }
    for (i = 0; i < DOOM_WORLD_MAX_HIT_FX; i++)
    {
        const DoomHitFx *effect = &world->hit_fx[i];
        const char *name = doom_world_hit_fx_sprite(effect);
        if (name && sink(context, DOOM_SPRITE_HIT, name, effect->position))
            ++count;
    }
    for (i = 0; i < DOOM_WORLD_MAX_PICKUPS; i++)
    {
        const DoomPickup *pickup = &world->pickups[i];
        const char *name = pickup->active ? doom_world_pickup_sprite(pickup->kind) : 0;
        if (name && sink(context, DOOM_SPRITE_PICKUP, name, pickup->position))
            ++count;
    }
    return count;
}
