/* Doom Mode world and player tick coordination. GPL-2.0-or-later. */
#include "doom_world_runtime.h"
#include <math.h>
#include <string.h>

typedef struct RuntimeEventContext
{
    DoomWorldRuntime *runtime;
    DoomPlayer *player;
    const DoomWorldFrame *frame;
    DoomEventFn emit;
    void *context;
    int spawned_slot, spawned_weapon;
} RuntimeEventContext;
static int projectile(int weapon)
{
    return weapon == DOOM_ROCKET || weapon == DOOM_PLASMA || weapon == DOOM_BFG;
}
static int can_spawn(const DoomWorldRuntime *runtime)
{
    unsigned i;
    for (i = 0; i < DOOM_WORLD_MAX_PROJECTILES; i++)
        if (runtime->world.projectiles[i].state == DOOM_PROJECTILE_FREE)
            return 1;
    return 0;
}
static int projectile_available(const DoomWorldFrame *frame, int weapon)
{
    const DoomWorldHost *host = frame->host;
    if (!frame->world_active || !frame->projectile_supported || !host || !host->sweep || !host->damage)
        return 0;
    if (weapon == DOOM_ROCKET && (!host->nearby || !host->visible))
        return 0;
    if (weapon == DOOM_BFG && !host->aim)
        return 0;
    return 1;
}
static int valid_shot(const DoomWorldFrame *frame)
{
    DoomVec3 o = frame->shot_origin, d = frame->shot_direction;
    return isfinite(o.x) && isfinite(o.y) && isfinite(o.z) && isfinite(d.x) && isfinite(d.y) &&
           isfinite(d.z) && (d.x * d.x + d.y * d.y + d.z * d.z) > 0.000001f;
}
static void refund(DoomPlayer *player, int weapon)
{
    int index, cost;
    if (weapon == DOOM_ROCKET)
    {
        index = DOOM_ROCKETS;
        cost = 1;
    }
    else if (weapon == DOOM_PLASMA)
    {
        index = DOOM_CELLS;
        cost = 1;
    }
    else
    {
        index = DOOM_CELLS;
        cost = 40;
    }
    player->ammo[index] += cost;
    if (player->ammo[index] > player->max_ammo[index])
        player->ammo[index] = player->max_ammo[index];
}
static void world_event(void *opaque, const DoomEvent *event)
{
    RuntimeEventContext *ctx = (RuntimeEventContext *)opaque;
    if (event && event->type == DOOM_EVENT_ATTACK && projectile(event->weapon))
    {
        if (!projectile_available(ctx->frame, event->weapon))
        {
            ctx->runtime->flags |= DOOM_WORLD_RUNTIME_UNSUPPORTED_PROJECTILE;
            refund(ctx->player, event->weapon);
        }
        else if (!valid_shot(ctx->frame))
        {
            ctx->runtime->flags |= DOOM_WORLD_RUNTIME_INVALID_ORIGIN;
            refund(ctx->player, event->weapon);
        }
        else
        {
            unsigned i;
            for (i = 0; i < DOOM_WORLD_MAX_PROJECTILES; ++i)
                if (ctx->runtime->world.projectiles[i].state == DOOM_PROJECTILE_FREE) break;
            if (i == DOOM_WORLD_MAX_PROJECTILES ||
                !doom_world_fire(&ctx->runtime->world, (DoomWeapon)event->weapon,
                                 ctx->frame->shot_origin, ctx->frame->shot_direction)) {
                ctx->runtime->flags |= DOOM_WORLD_RUNTIME_PROJECTILE_CAPACITY;
                refund(ctx->player, event->weapon);
            } else {
                ctx->spawned_slot = (int)i;
                ctx->spawned_weapon = event->weapon;
            }
        }
        return;
    }
    if (ctx->emit)
        ctx->emit(ctx->context, event);
}
void doom_world_runtime_reset(DoomWorldRuntime *runtime, uint64_t generation)
{
    if (!runtime)
        return;
    memset(runtime, 0, sizeof(*runtime));
    doom_world_reset(&runtime->world, generation);
}
int doom_world_runtime_death(DoomWorldRuntime *runtime, DoomPlayer *player, uint64_t death_id,
                             DoomVec3 position)
{
    return runtime ? doom_world_enemy_killed(&runtime->world, player, death_id, position) : 0;
}
void doom_world_runtime_advance(DoomWorldRuntime *runtime, DoomPlayer *player, unsigned elapsed_ms,
                                const DoomWorldFrame *frame, DoomEventFn emit, DoomPickupFn pickup,
                                void *context)
{
    RuntimeEventContext ctx;
    unsigned ticks, i;
    int fire;
    if (!runtime || !player || !frame)
        return;
    if (frame->world_active && (!frame->host || !isfinite(frame->player_position.x) || !isfinite(frame->player_position.y) ||
        !isfinite(frame->player_position.z) || !isfinite(frame->player_height) ||
        frame->player_height <= 0.0f))
    {
        runtime->flags |= DOOM_WORLD_RUNTIME_INVALID_PLAYER;
        return;
    }
    if (elapsed_ms > 250)
        elapsed_ms = 250;
    runtime->accumulator += elapsed_ms * 35u;
    ticks = runtime->accumulator / 1000u;
    runtime->accumulator %= 1000u;
    ctx.runtime = runtime;
    ctx.player = player;
    ctx.frame = frame;
    ctx.emit = emit;
    ctx.context = context;
    while (ticks--)
    {
        unsigned char before[DOOM_WORLD_MAX_PICKUPS];
        ctx.spawned_slot = -1;
        fire = frame->fire;
        if (fire && projectile(player->weapon))
        {
            if (!projectile_available(frame, player->weapon))
            {
                runtime->flags |= DOOM_WORLD_RUNTIME_UNSUPPORTED_PROJECTILE;
                fire = 0;
            }
            else if (!valid_shot(frame))
            {
                runtime->flags |= DOOM_WORLD_RUNTIME_INVALID_ORIGIN;
                fire = 0;
            }
            else if (!can_spawn(runtime))
            {
                runtime->flags |= DOOM_WORLD_RUNTIME_PROJECTILE_CAPACITY;
                fire = 0;
            }
        }
        for (i = 0; i < DOOM_WORLD_MAX_PICKUPS; i++)
            before[i] = (unsigned char)runtime->world.pickups[i].active;
        doom_tick(player, fire, world_event, &ctx);
        if (frame->world_active)
            doom_world_tick(&runtime->world, player, frame->player_position, frame->player_height,
                            frame->host);
        if (ctx.spawned_slot >= 0 &&
            runtime->world.projectiles[ctx.spawned_slot].state == DOOM_PROJECTILE_FREE) {
            refund(player, ctx.spawned_weapon);
            runtime->flags |= DOOM_WORLD_RUNTIME_UNSUPPORTED_PROJECTILE;
        }
        if (pickup)
            for (i = 0; i < DOOM_WORLD_MAX_PICKUPS; i++)
                if (before[i] && !runtime->world.pickups[i].active &&
                    runtime->world.pickups[i].age <= 35u * 60u)
                    pickup(context, runtime->world.pickups[i].kind);
    }
}
