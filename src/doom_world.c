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
unsigned doom_world_drain_impact_sounds(DoomWorld *world, DoomImpactSound *out, unsigned capacity)
{
    unsigned count = 0;
    if (!world || !out) return 0;
    while (count < capacity && world->impact_sound_count)
    {
        out[count++] = world->impact_sounds[world->impact_sound_head];
        world->impact_sound_head = (world->impact_sound_head + 1u) % DOOM_WORLD_MAX_IMPACT_SOUNDS;
        --world->impact_sound_count;
    }
    return count;
}
void doom_world_add_hit_fx(DoomWorld *world, DoomImpactKind kind, DoomVec3 position)
{
    unsigned i, slot=0;
    if (!world || !valid_vec(position) || (kind!=DOOM_IMPACT_PUFF && kind!=DOOM_IMPACT_BLOOD)) return;
    for (i=0;i<DOOM_WORLD_MAX_HIT_FX;++i) {
        if (!world->hit_fx[i].active) { slot=i; break; }
        if (world->hit_fx[i].age>world->hit_fx[slot].age) slot=i;
    }
    world->hit_fx[slot].active=1;
    world->hit_fx[slot].age=0;
    world->hit_fx[slot].kind=kind;
    world->hit_fx[slot].position=position;
}
const char *doom_world_hit_fx_sprite(const DoomHitFx *effect)
{
    static const char *const puff[]={"PUFFA0","PUFFB0","PUFFC0","PUFFD0"};
    static const char *const blood[]={"BLUDC0","BLUDB0","BLUDA0"};
    if (!effect || !effect->active) return 0;
    if (effect->kind==DOOM_IMPACT_PUFF && effect->age<16) return puff[effect->age/4];
    if (effect->kind==DOOM_IMPACT_BLOOD && effect->age<24) return blood[effect->age/8];
    return 0;
}
static void queue_impact_sound(DoomWorld *world, DoomWeapon weapon, DoomVec3 position)
{
    unsigned slot;
    DoomImpactSound *event;
    if (world->impact_sound_count >= DOOM_WORLD_MAX_IMPACT_SOUNDS) return;
    slot = (world->impact_sound_head + world->impact_sound_count) % DOOM_WORLD_MAX_IMPACT_SOUNDS;
    event = &world->impact_sounds[slot];
    event->weapon = weapon;
    event->position = position;
    event->lump = weapon == DOOM_ROCKET ? "DSBAREXP" : weapon == DOOM_PLASMA ? "DSFIRXPL" : "DSRXPLOD";
    ++world->impact_sound_count;
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
static void rocket_splash(DoomWorld *world, DoomVec3 pos, const DoomWorldHost *host,
                          uint64_t direct_target, int direct_damage, int direct_result)
{
    DoomTarget targets[DOOM_WORLD_MAX_SPLASH_TARGETS];
    unsigned count = 0, i;
#if defined(DOOM_MODE_LAB)
    DoomSplashTelemetry *telemetry;
    if (++world->splash_sequence == 0) ++world->splash_sequence;
    telemetry=&world->splash_history[(world->splash_sequence-1u)%DOOM_SPLASH_HISTORY];
    memset(telemetry,0,sizeof(*telemetry));
    telemetry->sequence=world->splash_sequence;
    telemetry->impact=pos;
    telemetry->direct_target=direct_target;
    telemetry->direct_damage=direct_damage;
    telemetry->direct_result=direct_result;
#else
    (void)direct_target; (void)direct_damage; (void)direct_result;
#endif
    if (!host->nearby || !host->visible || !host->damage ||
        !host->nearby(host->context, pos, 128.0f, targets, DOOM_WORLD_MAX_SPLASH_TARGETS, &count) ||
        count > DOOM_WORLD_MAX_SPLASH_TARGETS)
    {
        world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
#if defined(DOOM_MODE_LAB)
        telemetry->query_failed=1;
#endif
        return;
    }
#if defined(DOOM_MODE_LAB)
    telemetry->query_ok=1;
    telemetry->candidates=count;
    telemetry->at_capacity=count==DOOM_WORLD_MAX_SPLASH_TARGETS;
#endif
    for (i = 0; i < count; i++)
    {
        DoomTarget *t = &targets[i];
        float dx, dy, dist;
        int damage, visible, applied;
#if defined(DOOM_MODE_LAB)
        DoomSplashSample *sample=0;
        if (telemetry->sample_count<DOOM_SPLASH_SAMPLE_LIMIT) {
            sample=&telemetry->samples[telemetry->sample_count++];
            sample->target=t->handle; sample->radius=t->radius;
        }
#endif
        if (!t->handle || !valid_vec(t->position) || !isfinite(t->radius) || t->radius < 0 ||
            t->radius > 128)
        {
#if defined(DOOM_MODE_LAB)
            ++telemetry->invalid;
            if (sample) sample->state=DOOM_SPLASH_INVALID;
#endif
            continue;
        }
        dx = fabsf(t->position.x - pos.x);
        dy = fabsf(t->position.y - pos.y);
        dist = fmaxf(dx, dy) - t->radius;
        if (dist < 0)
            dist = 0;
#if defined(DOOM_MODE_LAB)
        if (sample) { sample->center_distance=fmaxf(dx,dy); sample->edge_distance=dist; }
#endif
        if (dist >= 128)
        {
#if defined(DOOM_MODE_LAB)
            ++telemetry->outside;
            if (sample) sample->state=DOOM_SPLASH_OUTSIDE;
#endif
            continue;
        }
        visible = host->visible(host->context, pos, t->handle);
        if (visible < 0)
        {
            world->errors |= DOOM_WORLD_ERROR_HOST_QUERY;
#if defined(DOOM_MODE_LAB)
            ++telemetry->query_failed;
            if (sample) sample->state=DOOM_SPLASH_QUERY_FAILED;
#endif
            continue;
        }
        if (!visible)
        {
#if defined(DOOM_MODE_LAB)
            ++telemetry->blocked;
            if (sample) sample->state=DOOM_SPLASH_BLOCKED;
#endif
            continue;
        }
        damage = 128 - (int)dist;
        applied=host->damage(host->context, t->handle, damage, DOOM_ROCKET);
#if defined(DOOM_MODE_LAB)
        ++telemetry->visible;
        if (sample) sample->damage=damage;
        if (applied) {
            ++telemetry->damaged;
            if (sample) sample->state=DOOM_SPLASH_DAMAGED;
        } else {
            ++telemetry->damage_failed;
            if (sample) sample->state=DOOM_SPLASH_DAMAGE_FAILED;
        }
#endif
        if (!applied)
            world->errors |= DOOM_WORLD_ERROR_HOST_DAMAGE;
    }
}
static void bfg_hit_effect(DoomWorld *world, DoomVec3 position)
{
    unsigned i, slot=0;
    for (i=0;i<DOOM_WORLD_MAX_BFG_HIT_FX;i++) {
        if (!world->bfg_hit_fx[i].active) { slot=i; break; }
        if (world->bfg_hit_fx[i].age>world->bfg_hit_fx[slot].age) slot=i;
    }
    world->bfg_hit_fx[slot].active=1;
    world->bfg_hit_fx[slot].age=0;
    world->bfg_hit_fx[slot].position=position;
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
        if (valid_vec(hit.point)) bfg_hit_effect(world,hit.point);
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
    int base, direct_damage=0, direct_result=-1;
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
    queue_impact_sound(world, p->weapon, p->position);
    if (trace.target)
    {
        base = p->weapon == DOOM_ROCKET ? 20 : p->weapon == DOOM_PLASMA ? 5 : 100;
        if (host->damage) {
            direct_damage=((int)(doom_random(player) & 7u) + 1)*base;
            direct_result=host->damage(host->context,trace.target,direct_damage,p->weapon);
        } else direct_result=0;
        if (!direct_result)
            world->errors |= DOOM_WORLD_ERROR_HOST_DAMAGE;
    }
    if (p->weapon == DOOM_ROCKET)
        rocket_splash(world, p->position, host, trace.target, direct_damage, direct_result);
}
void doom_world_tick_with_pickups(DoomWorld *world, DoomPlayer *player, DoomVec3 position, float height,
                                  const DoomWorldHost *host, DoomPickupFn pickup, void *context)
{
    unsigned i;
    if (!world || !player || !host || !valid_vec(position) || !isfinite(height) || height <= 0)
        return;
    for (i=0;i<DOOM_WORLD_MAX_BFG_HIT_FX;i++)
        if (world->bfg_hit_fx[i].active && ++world->bfg_hit_fx[i].age>=32)
            world->bfg_hit_fx[i].active=0;
    for (i=0;i<DOOM_WORLD_MAX_HIT_FX;i++)
        if (world->hit_fx[i].active && ++world->hit_fx[i].age>=
            (world->hit_fx[i].kind==DOOM_IMPACT_BLOOD ? 24u : 16u))
            world->hit_fx[i].active=0;
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
        if (p->kind >= DOOM_DROP_BULLETS && p->kind <= DOOM_DROP_GREEN_ARMOR &&
            player->health > 0 && fabsf(dx) <= 36 && fabsf(dy) <= 36 && dz >= -8 && dz <= height)
        {
            int before = p->kind <= DOOM_DROP_CELLS ? player->ammo[(int)p->kind] :
                         p->kind == DOOM_DROP_MEDIKIT ? player->health : player->armor;
            if (doom_collect(player, p->kind))
            {
                int after = p->kind <= DOOM_DROP_CELLS ? player->ammo[(int)p->kind] :
                            p->kind == DOOM_DROP_MEDIKIT ? player->health : player->armor;
                p->active = 0;
                if (pickup)
                    pickup(context, p->kind, after - before);
            }
        }
    }
}
void doom_world_tick(DoomWorld *world, DoomPlayer *player, DoomVec3 position, float height,
                     const DoomWorldHost *host)
{
    doom_world_tick_with_pickups(world, player, position, height, host, 0, 0);
}
const char *doom_world_pickup_sprite(DoomDrop kind)
{
    static const char *const names[] = {"CLIPA0", "SHELA0", "ROCKA0", "CELLA0", "MEDIA0", "ARM1A0"};
    return kind >= DOOM_DROP_BULLETS && kind <= DOOM_DROP_GREEN_ARMOR ? names[(int)kind] : 0;
}
const char *doom_world_bfg_hit_sprite(const DoomBfgHitFx *effect)
{
    static const char *const names[]={"BFE2A0","BFE2B0","BFE2C0","BFE2D0"};
    return effect && effect->active && effect->age<32 ? names[effect->age/8] : 0;
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
