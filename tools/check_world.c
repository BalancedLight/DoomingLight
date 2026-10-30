#include "doom_world.h"
#include "doom_world_runtime.h"
#include "doom_world_sprites.h"
#include <assert.h>
#include <string.h>
static int damage_calls, damage_total, fail_sweep, aim_calls, pickup_calls, sprite_calls;
static int pickup_amount;
static void pickup(void *context, DoomDrop kind, int amount)
{ (void)context; (void)kind; ++pickup_calls; pickup_amount = amount; }
static int sprite(void *context, DoomSpriteKind kind, const char *lump, DoomVec3 position)
{ (void)context; (void)kind; (void)position; assert(lump && *lump); ++sprite_calls; return 1; }
static int sweep(void *context, DoomVec3 from, DoomVec3 to, float radius, DoomTrace *out)
{
    (void)context;
    (void)from;
    (void)radius;
    if (fail_sweep)
        return 0;
    out->hit = 1;
    out->point = to;
    out->target = 42;
    return 1;
}
static int nearby(void *context, DoomVec3 origin, float radius, DoomTarget *out, unsigned cap,
                  unsigned *count)
{
    (void)context;
    (void)radius;
    assert(cap > 0);
    out[0].handle = 42;
    out[0].position = origin;
    out[0].radius = 16;
    *count = 1;
    return 1;
}
static int visible(void *context, DoomVec3 from, uint64_t target)
{
    (void)context;
    (void)from;
    (void)target;
    return 1;
}
static int aim(void *context, DoomVec3 origin, DoomVec3 direction, float range, DoomTrace *out)
{
    (void)context; (void)origin; (void)direction;
    assert(range == 1024.0f); ++aim_calls; out->hit = 1; out->target = 42; return 1;
}
static int damage(void *context, uint64_t target, int amount, DoomWeapon weapon)
{
    (void)context;
    assert(target == 42 && amount > 0 && (weapon == DOOM_ROCKET || weapon == DOOM_BFG || weapon == DOOM_PLASMA));
    ++damage_calls;
    damage_total += amount;
    return 1;
}
int main(void)
{
    DoomWorld world;
    DoomPlayer player;
    DoomVec3 start = {0, 0, 0}, dir = {1, 0, 0}, drop = {100, 0, 0};
    DoomWorldHost host;
    DoomWorldRuntime runtime;
    DoomWorldFrame frame;
    DoomImpactSound sounds[3];
    unsigned i;
    memset(&host, 0, sizeof(host));
    host.sweep = sweep;
    host.aim = aim;
    host.nearby = nearby;
    host.visible = visible;
    host.damage = damage;
    doom_reset(&player, 1);
    doom_world_reset(&world, 1);
    doom_world_add_hit_fx(&world, DOOM_IMPACT_PUFF, start);
    doom_world_add_hit_fx(&world, DOOM_IMPACT_BLOOD, drop);
    assert(!strcmp(doom_world_hit_fx_sprite(&world.hit_fx[0]), "PUFFA0"));
    assert(!strcmp(doom_world_hit_fx_sprite(&world.hit_fx[1]), "BLUDC0"));
    assert(doom_world_fire(&world, DOOM_ROCKET, start, dir));
    doom_world_tick(&world, &player, start, 56, &host);
    assert(damage_calls == 2 && damage_total >= 148 &&
           world.projectiles[0].state == DOOM_PROJECTILE_IMPACT && !world.errors);
#if defined(DOOM_MODE_LAB)
    assert(world.splash_sequence == 1 && world.splash_history[0].query_ok);
    assert(world.splash_history[0].direct_target == 42 && world.splash_history[0].direct_result == 1);
    assert(world.splash_history[0].candidates == 1 && world.splash_history[0].damaged == 1);
    assert(world.splash_history[0].samples[0].state == DOOM_SPLASH_DAMAGED);
#endif
    player.ammo[0] = 0;
    assert(doom_world_enemy_killed(&world, &player, 1, drop));
    assert(!doom_world_enemy_killed(&world, &player, 1, drop));
    doom_world_tick(&world, &player, drop, 56, &host);
    assert(player.ammo[0] == 10 && !world.pickups[0].active);
    assert(doom_world_drain_impact_sounds(&world, sounds, 3) == 1);
    assert(sounds[0].weapon == DOOM_ROCKET && sounds[0].position.x == 20.0f &&
           !strcmp(sounds[0].lump, "DSBAREXP"));
    assert(!doom_world_drain_impact_sounds(&world, sounds, 3));
    assert(world.hit_fx[0].age==2 && world.hit_fx[1].age==2);
    doom_world_reset(&world, 2);
    assert(world.generation == 2 && world.projectiles[0].state == DOOM_PROJECTILE_FREE);
    assert(!world.hit_fx[0].active && !world.hit_fx[1].active);
    damage_calls = 0;
    assert(doom_world_fire(&world, DOOM_BFG, start, dir));
    doom_world_tick(&world, &player, start, 56, &host);
    assert(damage_calls == 1);
    for (i = 0; i < 16; ++i)
        doom_world_tick(&world, &player, start, 56, &host);
    assert(aim_calls == 40 && damage_calls == 41 && !world.errors);
    for (i = 0; i < DOOM_WORLD_MAX_BFG_HIT_FX; ++i)
        assert(world.bfg_hit_fx[i].active && world.bfg_hit_fx[i].age == 0 &&
               !strcmp(doom_world_bfg_hit_sprite(&world.bfg_hit_fx[i]), "BFE2A0"));
    for (i = 0; i < 8; ++i)
        doom_world_tick(&world, &player, start, 56, &host);
    assert(!strcmp(doom_world_bfg_hit_sprite(&world.bfg_hit_fx[0]), "BFE2B0"));
    for (i = 8; i < 32; ++i)
        doom_world_tick(&world, &player, start, 56, &host);
    assert(!world.bfg_hit_fx[0].active && !doom_world_bfg_hit_sprite(&world.bfg_hit_fx[0]));
    assert(doom_world_drain_impact_sounds(&world, sounds, 3) == 1 &&
           !strcmp(sounds[0].lump, "DSRXPLOD"));
    assert(doom_world_fire(&world, DOOM_PLASMA, start, dir));
    doom_world_tick(&world, &player, start, 56, &host);
    assert(doom_world_drain_impact_sounds(&world, sounds, 3) == 1 &&
           !strcmp(sounds[0].lump, "DSFIRXPL"));
    doom_world_reset(&world, 3);
    for (i = 0; i < DOOM_WORLD_MAX_PROJECTILES; i++)
        assert(doom_world_fire(&world, DOOM_PLASMA, start, dir));
    assert(!doom_world_fire(&world, DOOM_PLASMA, start, dir));
    fail_sweep = 1;
    doom_world_tick(&world, &player, start, 56, &host);
    assert(world.errors & DOOM_WORLD_ERROR_HOST_QUERY);
    assert(world.projectiles[0].state == DOOM_PROJECTILE_FREE);
    fail_sweep = 0;
    memset(&frame, 0, sizeof(frame));
    frame.player_height = 56; frame.host = &host; frame.fire = 1; frame.world_active = 1;
    frame.shot_direction = dir;
    doom_reset(&player, 1); player.weapon = DOOM_ROCKET;
    doom_world_runtime_reset(&runtime, 10);
    doom_world_runtime_advance(&runtime, &player, 200, &frame, 0, pickup, 0);
    assert(player.ammo[DOOM_ROCKETS] == 50);
    assert(runtime.flags & DOOM_WORLD_RUNTIME_UNSUPPORTED_PROJECTILE);
    frame.projectile_supported = 1;
    doom_world_runtime_advance(&runtime, &player, 200, &frame, 0, pickup, 0);
    doom_world_runtime_advance(&runtime, &player, 200, &frame, 0, pickup, 0);
    assert(player.ammo[DOOM_ROCKETS] == 49);
    assert(runtime.world.projectiles[0].state == DOOM_PROJECTILE_IMPACT);
    assert(doom_world_render_sprites(&runtime.world, sprite, 0) == 1 && sprite_calls == 1);
    player.ammo[DOOM_BULLETS] = 0;
    assert(doom_world_runtime_death(&runtime, &player, 2, start));
    doom_world_runtime_advance(&runtime, &player, 29, &frame, 0, pickup, 0);
    assert(pickup_calls == 1 && player.ammo[DOOM_BULLETS] == 10);
    player.ammo[DOOM_BULLETS] = player.max_ammo[DOOM_BULLETS] - 3;
    runtime.world.pickups[0].active = 1;
    runtime.world.pickups[0].kind = DOOM_DROP_BULLETS;
    runtime.world.pickups[0].position = start;
    runtime.world.pickups[0].age = 0;
    doom_world_runtime_advance(&runtime, &player, 29, &frame, 0, pickup, 0);
    assert(pickup_calls == 2 && pickup_amount == 3);
    runtime.world.pickups[0].active = 1;
    runtime.world.pickups[0].age = 35u * 60u;
    doom_world_runtime_advance(&runtime, &player, 29, &frame, 0, pickup, 0);
    assert(!runtime.world.pickups[0].active && pickup_calls == 2);
    doom_world_runtime_reset(&runtime, 11);
    doom_reset(&player, 1);
    player.weapon = DOOM_ROCKET;
    fail_sweep = 1;
    doom_world_runtime_advance(&runtime, &player, 200, &frame, 0, pickup, 0);
    doom_world_runtime_advance(&runtime, &player, 200, &frame, 0, pickup, 0);
    assert(player.ammo[DOOM_ROCKETS] == 50);
    assert(runtime.flags & DOOM_WORLD_RUNTIME_UNSUPPORTED_PROJECTILE);
    assert(runtime.world.errors & DOOM_WORLD_ERROR_HOST_QUERY);
    return 0;
}
