/* Doom gameplay logic, copyright 1993-1996 id Software, Inc.
 * Distributed under the GNU General Public License, version 2 or later. */
#include "doom_world.h"
#include <math.h>
#include <string.h>

#define DOOM_PI 3.14159265358979323846f
#define PROJECTILE_LIFETIME (35u * 30u)
#define PICKUP_LIFETIME (35u * 60u)

static int valid_vec(DoomVec3 v)
{
    return isfinite(v.x) && isfinite(v.y) && isfinite(v.z);
}
static DoomVec3 add(DoomVec3 a, DoomVec3 b)
{
    DoomVec3 v = {a.x + b.x, a.y + b.y, a.z + b.z};
    return v;
}
static DoomVec3 scaled(DoomVec3 a, float n)
{
    DoomVec3 v = {a.x * n, a.y * n, a.z * n};
    return v;
}
void doom_world_reset(DoomWorld *world, uint64_t generation)
{
    if (!world)
        return;
    memset(world, 0, sizeof(*world));
    world->generation = generation;
}
int doom_world_fire(DoomWorld *world, DoomWeapon weapon, DoomVec3 origin, DoomVec3 direction)
{
    float length;
    unsigned i;
    DoomProjectile *p;
    if (!world || (weapon != DOOM_ROCKET && weapon != DOOM_PLASMA && weapon != DOOM_BFG) ||
        !valid_vec(origin) || !valid_vec(direction))
        return 0;
    length =
        sqrtf(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
    if (!isfinite(length) || length < 0.0001f)
        return 0;
    for (i = 0; i < DOOM_WORLD_MAX_PROJECTILES; i++)
        if (world->projectiles[i].state == DOOM_PROJECTILE_FREE)
        {
            p = &world->projectiles[i];
            memset(p, 0, sizeof(*p));
            p->state = DOOM_PROJECTILE_FLYING;
            p->weapon = weapon;
            p->position = origin;
            p->origin = origin;
            p->direction = scaled(direction, 1.0f / length);
            return 1;
        }
    return 0;
}
int doom_world_enemy_killed(DoomWorld *world, DoomPlayer *player, uint64_t death_id,
                            DoomVec3 position)
{
    unsigned i;
    DoomDrop kind;
    if (!world || !player || !death_id || !valid_vec(position))
        return 0;
    for (i = 0; i < 128; i++)
        if (world->killed[i] == death_id)
            return 0;
    world->killed[world->killed_next++ % 128] = death_id;
    kind = doom_enemy_killed(player);
    for (i = 0; i < DOOM_WORLD_MAX_PICKUPS; i++)
        if (!world->pickups[i].active)
        {
            world->pickups[i].active = 1;
            world->pickups[i].kind = kind;
            world->pickups[i].position = position;
            world->pickups[i].age = 0;
            return 1;
        }
    return 0;
}
static void rocket_splash(DoomWorld *world, DoomVec3 pos, const DoomWorldHost *host)
{
    DoomTarget targets[DOOM_WORLD_MAX_SPLASH_TARGETS];
    unsigned count = 0, i;
    if (!host->nearby || !host->visible || !host->damage ||
        !host->nearby(host->context, pos, 128.0f, targets, DOOM_WORLD_MAX_SPLASH_TARGETS, &count) ||
        count > DOOM_WORLD_MAX_SPLASH_TARGETS)
    {
        world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
        return;
    }
    for (i = 0; i < count; i++)
    {
        DoomTarget *t = &targets[i];
        float dx, dy, dist;
        int damage, visible;
        if (!t->handle || !valid_vec(t->position) || !isfinite(t->radius) || t->radius < 0 ||
            t->radius > 128)
            continue;
        dx = fabsf(t->position.x - pos.x);
        dy = fabsf(t->position.y - pos.y);
        dist = fmaxf(dx, dy) - t->radius;
        if (dist < 0)
            dist = 0;
        if (dist >= 128)
            continue;
        visible = host->visible(host->context, pos, t->handle);
        if (visible < 0)
        {
            world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
            continue;
        }
        if (!visible)
            continue;
        damage = 128 - (int)dist;
        if (!host->damage(host->context, t->handle, damage, DOOM_ROCKET))
            world->errors |= DOOM_WORLD_ERROR_HOST_DAMAGE;
    }
}
static void bfg_spray(DoomWorld *world, DoomPlayer *player, DoomProjectile *p, DoomVec3 aim_origin,
                      const DoomWorldHost *host)
{
    unsigned i, j;
    float heading;
    if (!host->aim || !host->damage)
    {
        world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
        return;
    }
    heading = atan2f(p->direction.y, p->direction.x);
    for (i = 0; i < 40; i++)
    {
        float angle = heading - DOOM_PI / 4.0f + (DOOM_PI / 2.0f) * (float)i / 40.0f;
        DoomVec3 direction = {cosf(angle), sinf(angle), 0};
        DoomTrace hit;
        int damage = 0;
        memset(&hit, 0, sizeof(hit));
        if (!host->aim(host->context, aim_origin, direction, 1024.0f, &hit))
        {
            world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
            return;
        }
        if (!hit.hit || !hit.target)
            continue;
        for (j = 0; j < 15; j++)
            damage += (int)(doom_random(player) & 7u) + 1;
        if (!host->damage(host->context, hit.target, damage, DOOM_BFG))
            world->errors |= DOOM_WORLD_ERROR_HOST_DAMAGE;
    }
}
static void tick_projectile(DoomWorld *world, DoomProjectile *p, DoomPlayer *player,
                            DoomVec3 aim_origin, const DoomWorldHost *host)
{
    DoomTrace trace;
    DoomVec3 next;
    float speed, radius;
    int base;
    if (p->state == DOOM_PROJECTILE_FREE)
        return;
    if (p->state == DOOM_PROJECTILE_IMPACT)
    {
        ++p->impact_age;
        if (p->weapon == DOOM_ROCKET)
        {
            p->frame = p->impact_age < 8 ? 0 : p->impact_age < 14 ? 1 : 2;
            if (p->impact_age >= 18)
                p->state = DOOM_PROJECTILE_FREE;
        }
        else if (p->weapon == DOOM_PLASMA)
        {
            p->frame = (int)(p->impact_age / 4);
            if (p->impact_age >= 20)
                p->state = DOOM_PROJECTILE_FREE;
        }
        else
        {
            p->frame = (int)(p->impact_age / 8);
            if (p->impact_age >= 16 && !p->spray_done)
            {
                p->spray_done = 1;
                bfg_spray(world, player, p, aim_origin, host);
            }
            if (p->impact_age >= 48)
                p->state = DOOM_PROJECTILE_FREE;
        }
        return;
    }
    if (++p->age > PROJECTILE_LIFETIME)
    {
        p->state = DOOM_PROJECTILE_FREE;
        return;
    }
    if (!host->sweep)
    {
        world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
        p->state = DOOM_PROJECTILE_FREE;
        return;
    }
    speed = p->weapon == DOOM_ROCKET ? 20.0f : 25.0f;
    radius = p->weapon == DOOM_ROCKET ? 11.0f : 13.0f;
    next = add(p->position, scaled(p->direction, speed));
    memset(&trace, 0, sizeof(trace));
    if (!host->sweep(host->context, p->position, next, radius, &trace))
    {
        world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
        p->state = DOOM_PROJECTILE_FREE;
        return;
    }
    if (!trace.hit)
    {
        p->position = next;
        p->frame = (int)((p->age / (p->weapon == DOOM_BFG ? 4u : 6u)) % 2u);
        return;
    }
    if (!valid_vec(trace.point))
    {
        world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
        p->state = DOOM_PROJECTILE_FREE;
        return;
    }
    p->position = trace.point;
    p->state = DOOM_PROJECTILE_IMPACT;
    p->frame = 0;
    p->impact_age = 0;
    if (trace.target)
    {
        base = p->weapon == DOOM_ROCKET ? 20 : p->weapon == DOOM_PLASMA ? 5 : 100;
        if (!host->damage || !host->damage(host->context, trace.target,
                                           ((int)(doom_random(player) & 7u) + 1) * base, p->weapon))
            world->errors |= DOOM_WORLD_ERROR_HOST_DAMAGE;
    }
    if (p->weapon == DOOM_ROCKET)
        rocket_splash(world, p->position, host);
}
void doom_world_tick(DoomWorld *world, DoomPlayer *player, DoomVec3 position, float height,
                     const DoomWorldHost *host)
{
    unsigned i;
    if (!world || !player || !host || !valid_vec(position) || !isfinite(height) || height <= 0)
        return;
    for (i = 0; i < DOOM_WORLD_MAX_PROJECTILES; i++)
    {
        DoomVec3 aim_origin = position;
        aim_origin.z += height * 0.5f + 8.0f;
        tick_projectile(world, &world->projectiles[i], player, aim_origin, host);
    }
    for (i = 0; i < DOOM_WORLD_MAX_PICKUPS; i++)
    {
        DoomPickup *p = &world->pickups[i];
        float dx, dy, dz;
        if (!p->active)
            continue;
        if (++p->age > PICKUP_LIFETIME)
        {
            p->active = 0;
            continue;
        }
        dx = p->position.x - position.x;
        dy = p->position.y - position.y;
        dz = p->position.z - position.z;
        if (player->health > 0 && fabsf(dx) <= 36 && fabsf(dy) <= 36 && dz >= -8 && dz <= height &&
            doom_collect(player, p->kind))
            p->active = 0;
    }
}
const char *doom_world_pickup_sprite(DoomDrop kind)
{
    static const char *const names[] = {"CLIPA0", "SHELA0", "ROCKA0", "CELLA0", "MEDIA0", "ARM1A0"};
    return kind >= DOOM_DROP_BULLETS && kind <= DOOM_DROP_GREEN_ARMOR ? names[(int)kind] : 0;
}
const char *doom_world_projectile_sprite(const DoomProjectile *p)
{
    static const char *const rocket[] = {"MISLB0", "MISLC0", "MISLD0"};
    static const char *const plasma[] = {"PLSEA0", "PLSEB0", "PLSEC0", "PLSED0", "PLSEE0"};
    static const char *const bfg[] = {"BFE1A0", "BFE1B0", "BFE1C0", "BFE1D0", "BFE1E0", "BFE1F0"};
    if (!p || p->state == DOOM_PROJECTILE_FREE || p->frame < 0)
        return 0;
    if (p->state == DOOM_PROJECTILE_FLYING)
    {
        if (p->weapon == DOOM_ROCKET)
            return "MISLA1";
        if (p->weapon == DOOM_PLASMA)
            return p->frame ? "PLSSB0" : "PLSSA0";
        return p->frame ? "BFS1B0" : "BFS1A0";
    }
    if (p->weapon == DOOM_ROCKET)
        return p->frame < 3 ? rocket[p->frame] : 0;
    if (p->weapon == DOOM_PLASMA)
        return p->frame < 5 ? plasma[p->frame] : 0;
    return p->frame < 6 ? bfg[p->frame] : 0;
}
