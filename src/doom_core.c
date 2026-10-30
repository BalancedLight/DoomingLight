/* Doom-derived gameplay values and rules. Copyright (C) 1993-1996 id Software, Inc.
 * Distributed under the GNU General Public License, version 2 or later. */
#include "doom_core.h"
#include <ctype.h>
#include <string.h>

static const unsigned char random_table[256] = {
    0,   8,   109, 220, 222, 241, 149, 107, 75,  248, 254, 140, 16,  66,  74,  21,  211, 47,  80,
    242, 154, 27,  205, 128, 161, 89,  77,  36,  95,  110, 85,  48,  212, 140, 211, 249, 22,  79,
    200, 50,  28,  188, 52,  140, 202, 120, 68,  145, 62,  70,  184, 190, 91,  197, 152, 224, 149,
    104, 25,  178, 252, 182, 202, 182, 141, 197, 4,   81,  181, 242, 145, 42,  39,  227, 156, 198,
    225, 193, 219, 93,  122, 175, 249, 0,   175, 143, 70,  239, 46,  246, 163, 53,  163, 109, 168,
    135, 2,   235, 25,  92,  20,  145, 138, 77,  69,  166, 78,  176, 173, 212, 166, 113, 94,  161,
    41,  50,  239, 49,  111, 164, 70,  60,  2,   37,  171, 75,  136, 156, 11,  56,  42,  146, 138,
    229, 73,  146, 77,  61,  98,  196, 135, 106, 63,  197, 195, 86,  96,  203, 113, 101, 170, 247,
    181, 113, 80,  250, 108, 7,   255, 237, 129, 226, 79,  107, 112, 166, 103, 241, 24,  223, 239,
    120, 198, 58,  60,  82,  128, 3,   184, 66,  143, 224, 145, 224, 81,  206, 163, 45,  63,  90,
    168, 114, 59,  33,  159, 95,  28,  139, 123, 98,  125, 196, 15,  70,  194, 253, 54,  14,  109,
    226, 71,  17,  161, 93,  186, 87,  244, 138, 20,  52,  123, 251, 26,  36,  17,  46,  52,  231,
    232, 76,  31,  221, 84,  37,  216, 165, 212, 106, 197, 242, 98,  43,  39,  175, 254, 145, 190,
    84,  118, 222, 187, 136, 120, 163, 236, 249};
static const int max_ammo[4] = {200, 50, 50, 300};
unsigned doom_random(DoomPlayer *player)
{
    if (!player)
        return 0;
    player->random_index = (player->random_index + 1u) & 255u;
    return random_table[player->random_index];
}
static const int amount[4] = {10, 4, 1, 20};
static const int ammo_kind[DOOM_WEAPON_COUNT] = {-1, 0, 1, 0, 2, 3, 3, -1, 1};
static const unsigned cost[DOOM_WEAPON_COUNT] = {0, 1, 1, 1, 1, 1, 40, 0, 2};
/* Time from trigger to first attack, and total duration in 35 Hz tics. */
static const unsigned delay[DOOM_WEAPON_COUNT] = {4, 4, 3, 0, 8, 0, 30, 0, 3};
static const unsigned duration[DOOM_WEAPON_COUNT] = {22, 19, 44, 8, 20, 3, 60, 8, 62};
static int available(const DoomPlayer *p, int weapon)
{
    int a;
    if (weapon < 0 || weapon >= DOOM_WEAPON_COUNT || !(p->owned_mask & (1u << weapon)))
        return 0;
    a = ammo_kind[weapon];
    return a < 0 || p->ammo[a] >= (int)cost[weapon];
}
static void send(DoomEventFn emit, void *ctx, DoomEventType type, int value, int weapon,
                 int pellets, int damage, int spread)
{
    DoomEvent e;
    if (!emit)
        return;
    e.type = type;
    e.value = value;
    e.weapon = weapon;
    e.pellets = pellets;
    e.damage = damage;
    e.spread = spread;
    e.vertical_spread = 0;
    emit(ctx, &e);
}
static int pain_base(const DoomPlayer *p)
{
    int health = p->health;
    if (health < 0)
        health = 0;
    if (health > 100)
        health = 100;
    return ((100 - health) * 5 / 101) * 8;
}
static void start_flash(DoomPlayer *p, int weapon, int variant)
{
    static const unsigned lengths[DOOM_WEAPON_COUNT] = {0, 7, 7, 5, 15, 4, 17, 0, 9};
    p->flash_weapon = weapon;
    p->flash_frame = variant;
    p->flash_tics = lengths[weapon];
}
static void update_flash(DoomPlayer *p)
{
    if (!p->flash_tics)
        return;
    --p->flash_tics;
    switch (p->flash_weapon)
    {
    case DOOM_SHOTGUN:
        p->flash_frame = p->flash_tics <= 3;
        break;
    case DOOM_SUPER_SHOTGUN:
        p->flash_frame = p->flash_tics <= 4;
        break;
    case DOOM_ROCKET:
        p->flash_frame = p->flash_tics <= 4    ? 3
                         : p->flash_tics <= 8  ? 2
                         : p->flash_tics <= 12 ? 1
                                               : 0;
        break;
    case DOOM_BFG:
        p->flash_frame = p->flash_tics <= 6;
        break;
    default:
        break;
    }
}
void doom_reset(DoomPlayer *p, int doom2)
{
    if (!p)
        return;
    memset(p, 0, sizeof(*p));
    p->doom2 = !!doom2;
    p->health = 100;
    p->weapon = DOOM_PISTOL;
    p->pending_weapon = -1;
    p->weapon_y = 32;
    p->view_height = 41;
    p->owned_mask = (1u << DOOM_WEAPON_COUNT) - 1u;
    if (!p->doom2)
        p->owned_mask &= ~(1u << DOOM_SUPER_SHOTGUN);
    memcpy(p->ammo, max_ammo, sizeof(max_ammo));
    memcpy(p->max_ammo, max_ammo, sizeof(max_ammo));
}
void doom_select_weapon(DoomPlayer *p, DoomWeapon w)
{
    if (p && available(p, (int)w))
        p->pending_weapon = (int)w;
}
void doom_select_slot(DoomPlayer *p, int slot)
{
    static const DoomWeapon slots[7] = {DOOM_FIST, DOOM_PISTOL, DOOM_SHOTGUN,
        DOOM_CHAINGUN, DOOM_ROCKET, DOOM_PLASMA, DOOM_BFG};
    DoomWeapon selected;
    if (!p || slot < 1 || slot > 7)
        return;
    selected = slots[slot - 1];
    if (slot == 1 && available(p, DOOM_CHAINSAW) && p->weapon != DOOM_CHAINSAW)
        selected = DOOM_CHAINSAW;
    if (slot == 3 && p->doom2 && available(p, DOOM_SUPER_SHOTGUN) &&
        p->weapon != DOOM_SUPER_SHOTGUN)
        selected = DOOM_SUPER_SHOTGUN;
    doom_select_weapon(p, selected);
}
static void fire_weapon(DoomPlayer *p, DoomEventFn emit, void *ctx)
{
    int w, a, pellets = 1, damage = 0, spread = 0;

    w = p->weapon;
    a = ammo_kind[w];
    p->shot_fired = 1;
    if (a >= 0)
    {
        if (p->ammo[a] < (int)cost[w])
            return;
        p->ammo[a] -= (int)cost[w];
    }
    switch (w)
    {
    case DOOM_FIST:
    case DOOM_CHAINSAW:
        damage = 2;
        spread = 1;
        break;
    case DOOM_PISTOL:
    case DOOM_CHAINGUN:
        damage = 5;
        spread = (int)(p->refire != 0);
        break;
    case DOOM_SHOTGUN:
        pellets = 7;
        damage = 5;
        spread = 1;
        break;
    case DOOM_SUPER_SHOTGUN:
        pellets = 20;
        damage = 5;
        spread = 2;
        break;
    case DOOM_ROCKET:
    case DOOM_PLASMA:
    case DOOM_BFG:
        damage = 0;
        break;
    }
    if (w == DOOM_PISTOL || w == DOOM_CHAINGUN)
        send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_PISTOL, w, 0, 0, 0);
    else if (w == DOOM_SHOTGUN)
        send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_SHOTGN, w, 0, 0, 0);
    else if (w == DOOM_SUPER_SHOTGUN)
        send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_DSHTGN, w, 0, 0, 0);
    else if (w == DOOM_CHAINSAW)
        send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_SAWFUL, w, 0, 0, 0);
    else if (w == DOOM_ROCKET)
        send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_RLAUNC, w, 0, 0, 0);
    else if (w == DOOM_PLASMA)
        send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_PLASMA, w, 0, 0, 0);
    if (w == DOOM_PISTOL || w == DOOM_SHOTGUN || w == DOOM_SUPER_SHOTGUN)
        start_flash(p, w, 0);
    else if (w == DOOM_CHAINGUN)
        start_flash(p, w, p->cooldown <= 4 ? 1 : 0);
    else if (w == DOOM_PLASMA)
    {
        p->random_index = (p->random_index + 1u) & 255u;
        start_flash(p, w, random_table[p->random_index] & 1);
    }
    if (w == DOOM_ROCKET || w == DOOM_PLASMA || w == DOOM_BFG)
    {
        send(emit, ctx, DOOM_EVENT_ATTACK, 0, w, 1, 0, 0);
    }
    else
    {
        int i;
        for (i = 0; i < pellets; i++)
        {
            int value, angle = 0, vertical = 0;
            p->random_index = (p->random_index + 1u) & 255u;
            value = (w == DOOM_FIST || w == DOOM_CHAINSAW)
                        ? ((random_table[p->random_index] % 10) + 1) * 2
                        : ((random_table[p->random_index] % 3) + 1) * damage;
            if (spread)
            {
                int first, second;
                p->random_index = (p->random_index + 1u) & 255u;
                first = random_table[p->random_index];
                p->random_index = (p->random_index + 1u) & 255u;
                second = random_table[p->random_index];
                angle = (first - second) * (spread == 2 ? 2 : 1);
            }
            if (w == DOOM_SUPER_SHOTGUN)
            {
                int first, second;
                p->random_index = (p->random_index + 1u) & 255u;
                first = random_table[p->random_index];
                p->random_index = (p->random_index + 1u) & 255u;
                second = random_table[p->random_index];
                vertical = first - second;
            }
            if (emit)
            {
                DoomEvent e = {DOOM_EVENT_ATTACK, 0, w, 1, value, angle, vertical};
                emit(ctx, &e);
            }
        }
    }
}

static void tick(DoomPlayer *p, int fire, DoomEventFn emit, void *ctx)
{
    if (p->health <= 0)
    {
        p->face = 2;
        p->face_index = 41;
        p->face_tics = 0;
        p->cooldown = 0;
        p->fire_delay = 0;
        p->shot_fired = 0;
        p->flash_tics = 0;
        p->pending_weapon = -1;
        p->switch_phase = 0;
        p->plasma_recovery = 0;
        p->attack_down = 0;
        p->refire = 0;
        p->face_attack_tics = 0;
        if (p->view_height > 6)
            --p->view_height;
        if (p->weapon_y < 128)
        {
            p->weapon_y += 6;
            if (p->weapon_y > 128)
                p->weapon_y = 128;
        }
        return;
    }
    if (p->invulnerability_tics)
        --p->invulnerability_tics;
    update_flash(p);
    p->face_random = (p->face_random + 1u) & 255u;
    if (fire) {
        if (p->face_attack_tics < 71) ++p->face_attack_tics;
    } else p->face_attack_tics = 0;
    if (p->face_tics) --p->face_tics;
    if (p->face == 1 && p->face_tics) {
        /* Damage expressions take priority over firing and god mode. */
    } else {
        p->face = 0;
        if (p->face_attack_tics > 70) {
            p->face_index = pain_base(p) + 7;
            p->face_tics = 1;
        } else if (p->god || p->invulnerability_tics) {
            p->face_index = 40;
            p->face_tics = 0;
        } else if (!p->face_tics) {
            p->face_index = pain_base(p) + random_table[p->face_random] % 3;
            p->face_tics = 17;
        }
    }
    if (p->switch_phase == 1)
    {
        if (p->weapon == DOOM_CHAINSAW) p->weapon_frame = 2;
        p->weapon_y += 6;
        if (p->weapon_y >= 128)
        {
            p->weapon_y = 128;
            if (available(p, p->pending_weapon))
                p->weapon = p->pending_weapon;
            p->weapon_frame = p->weapon == DOOM_CHAINSAW ? 2 : 0;
            p->pending_weapon = -1;
            p->switch_phase = 2;
            p->refire = 0;
            if (p->weapon == DOOM_CHAINSAW)
                send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_SAWUP, p->weapon, 0, 0, 0);
        }
        p->attack_down = !!fire;
        return;
    }
    if (p->switch_phase == 2)
    {
        p->weapon_y -= 6;
        if (p->weapon_y <= 32)
        {
            p->weapon_y = 32;
            p->switch_phase = 0;
        }
        p->attack_down = !!fire;
        return;
    }
    if (p->cooldown)
    {
        --p->cooldown;
        if (p->fire_delay)
            --p->fire_delay;
    }
    if (p->cooldown && !p->plasma_recovery)
    {
        unsigned elapsed = duration[p->weapon] - p->cooldown;
        if (p->weapon == DOOM_BFG && elapsed == 1)
            send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_BFG, p->weapon, 0, 0, 0);
        if (p->weapon == DOOM_BFG && elapsed == 20)
            start_flash(p, DOOM_BFG, 0);
        if (p->weapon == DOOM_SUPER_SHOTGUN && elapsed == 24)
            send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_DBOPN, p->weapon, 0, 0, 0);
        if (p->weapon == DOOM_SUPER_SHOTGUN && elapsed == 38)
            send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_DBLOAD, p->weapon, 0, 0, 0);
        if (p->weapon == DOOM_SUPER_SHOTGUN && elapsed == 51)
            send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_DBCLS, p->weapon, 0, 0, 0);
    }
    if (p->cooldown == 4 && p->shot_fired == 1 &&
        (p->weapon == DOOM_CHAINGUN || p->weapon == DOOM_CHAINSAW))
    {
        p->shot_fired = 0;
    }
    if (p->cooldown && !p->fire_delay && !p->shot_fired)
        fire_weapon(p, emit, ctx);

    if (p->cooldown)
    {
        static const unsigned char frames[DOOM_WEAPON_COUNT][10] = {
            {1, 2, 3, 2, 1, 0, 0, 0, 0, 0}, {0, 1, 2, 1, 0, 0, 0, 0, 0, 0},
            {0, 0, 1, 2, 3, 2, 1, 0, 0, 0}, {0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
            {1, 1, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 1, 1, 1, 0, 0, 0, 0, 0, 0}, {0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
            {0, 0, 1, 2, 3, 4, 5, 6, 7, 0}};
        static const unsigned char times[DOOM_WEAPON_COUNT][10] = {
            {4, 4, 5, 4, 5, 0, 0, 0, 0, 0},     {4, 6, 4, 5, 0, 0, 0, 0, 0, 0},
            {3, 7, 5, 5, 4, 5, 5, 3, 7, 0},     {4, 4, 0, 0, 0, 0, 0, 0, 0, 0},
            {8, 12, 0, 0, 0, 0, 0, 0, 0, 0},    {3, 20, 0, 0, 0, 0, 0, 0, 0, 0},
            {20, 10, 10, 20, 0, 0, 0, 0, 0, 0}, {4, 4, 0, 0, 0, 0, 0, 0, 0, 0},
            {3, 7, 7, 7, 7, 7, 7, 6, 6, 5}};
        unsigned elapsed = p->plasma_recovery ? 3 : duration[p->weapon] - p->cooldown, i;
        for (i = 0; i < 10 && times[p->weapon][i]; i++)
        {
            if (elapsed < times[p->weapon][i])
            {
                p->weapon_frame = frames[p->weapon][i];
                break;
            }
            elapsed -= times[p->weapon][i];
        }
    }
    if (!p->cooldown)
    {
        if (p->weapon == DOOM_PLASMA && p->shot_fired && !fire && !p->plasma_recovery)
        {
            p->plasma_recovery = 1;
            p->cooldown = 20;
            p->attack_down = 0;
            return;
        }
        p->plasma_recovery = 0;
        p->weapon_frame = 0;
        if (p->weapon == DOOM_CHAINSAW && !fire && !p->switch_phase)
        {
            if (!p->saw_idle_tics)
            {
                send(emit, ctx, DOOM_EVENT_SOUND, DOOM_SOUND_SAWIDL, p->weapon, 0, 0, 0);
                p->saw_idle_tics = 8;
            }
            p->weapon_frame = 2 + (int)((8 - p->saw_idle_tics) / 4);
            --p->saw_idle_tics;
        }
        else
            p->saw_idle_tics = 0;
        p->shot_fired = 0;
        if (fire && p->pending_weapon < 0 && !available(p, p->weapon))
        {
            static const int order[] = {DOOM_PLASMA,  DOOM_SUPER_SHOTGUN, DOOM_CHAINGUN,
                                        DOOM_SHOTGUN, DOOM_PISTOL,        DOOM_CHAINSAW,
                                        DOOM_ROCKET,  DOOM_BFG,           DOOM_FIST};
            unsigned i;
            for (i = 0; i < sizeof(order) / sizeof(order[0]); i++)
            {
                if (available(p, order[i]))
                {
                    p->pending_weapon = order[i];
                    break;
                }
            }
        }
        if (p->pending_weapon >= 0 && available(p, p->pending_weapon) &&
            p->weapon != p->pending_weapon)
        {
            p->switch_phase = 1;
            if (p->weapon == DOOM_CHAINSAW) p->weapon_frame = 2;
            p->attack_down = !!fire;
            return;
        }
        p->pending_weapon = -1;
        if (fire && available(p, p->weapon))
        {
            if (p->weapon == DOOM_CHAINSAW) p->weapon_frame = 0;
            p->fire_delay = delay[p->weapon];
            p->cooldown = duration[p->weapon];
            p->shot_fired = 0;
            if (p->weapon == DOOM_ROCKET)
                start_flash(p, DOOM_ROCKET, 0);
            if (p->attack_down)
                ++p->refire;
            else
                p->refire = 0;
            if (!p->fire_delay)
                fire_weapon(p, emit, ctx);
        }
    }
    p->attack_down = !!fire;
}
void doom_tick(DoomPlayer *p, int fire, DoomEventFn emit, void *ctx)
{
    if (p)
        tick(p, fire, emit, ctx);
}
void doom_advance(DoomPlayer *p, unsigned ms, int fire, DoomEventFn emit, void *ctx)
{
    unsigned tics;
    if (!p)
        return;
    if (ms > 250)
        ms = 250;
    p->accumulator += ms * 35u;
    tics = p->accumulator / 1000u;
    p->accumulator %= 1000u;
    while (tics--)
        tick(p, fire, emit, ctx);
}
int doom_damage_from(DoomPlayer *p, int damage, int attacker_side)
{
    int saved = 0;
    if (!p || damage <= 0 || p->health <= 0 || p->god || p->invulnerability_tics)
        return 0;
    if (p->armor_type)
    {
        saved = p->armor_type == 1 ? damage / 3 : damage / 2;
        if (saved >= p->armor)
        {
            saved = p->armor;
            p->armor_type = 0;
        }
        p->armor -= saved;
    }
    damage -= saved;
    p->health -= damage;
    if (p->health < 0)
        p->health = 0;
    p->face = p->health ? 1 : 2;
    p->face_tics = p->health ? 35u : 0u;
    p->face_index = p->health ? pain_base(p) + (damage > 20         ? 5
                                                : attacker_side < 0 ? 4
                                                : attacker_side > 0 ? 3
                                                                    : 7)
                              : 41;
    return damage;
}
int doom_damage(DoomPlayer *p, int damage)
{
    return doom_damage_from(p, damage, 0);
}
static int ends(const DoomPlayer *p, const char *s)
{
    size_t n = strlen(s);
    return p->cheat_len >= n && !memcmp(p->cheat + p->cheat_len - n, s, n);
}
void doom_cheat_char(DoomPlayer *p, char ch, DoomEventFn emit, void *ctx)
{
    unsigned i;
    int code = 0;
    if (!p || !isalnum((unsigned char)ch))
        return;
    ch = (char)toupper((unsigned char)ch);
    if (p->cheat_len == sizeof(p->cheat) - 1)
    {
        memmove(p->cheat, p->cheat + 1, --p->cheat_len);
    }
    p->cheat[p->cheat_len++] = ch;
    p->cheat[p->cheat_len] = 0;
    if (ends(p, "IDDQD"))
    {
        p->god = !p->god;
        if (p->god)
            p->health = 100;
        code = 1;
    }
    else if (ends(p, "IDFA") || ends(p, "IDKFA"))
    {
        if (ends(p, "IDKFA"))
            p->keys = 1;
        p->armor = 200;
        p->armor_type = 2;
        for (i = 0; i < 4; i++)
        {
            p->ammo[i] = p->max_ammo[i];
        }
        p->owned_mask = (1u << DOOM_WEAPON_COUNT) - 1;
        if (!p->doom2)
            p->owned_mask &= ~(1u << DOOM_SUPER_SHOTGUN);
        code = 2;
    }
    else if (ends(p, "IDCHOPPERS"))
    {
        p->owned_mask |= 1u << DOOM_CHAINSAW;
        p->invulnerability_tics = 1;
        code = 3;
    }
    else if (p->cheat_len >= 7 && !memcmp(p->cheat + p->cheat_len - 7, "IDMUS", 5) &&
             isdigit((unsigned char)p->cheat[p->cheat_len - 2]) &&
             isdigit((unsigned char)p->cheat[p->cheat_len - 1]))
    {
        int track = (p->cheat[p->cheat_len - 2] - '0') * 10 + p->cheat[p->cheat_len - 1] - '0';
        send(emit, ctx, DOOM_EVENT_MUSIC, track, p->weapon, 0, 0, 0);
        p->cheat_len = 0;
        return;
    }
    if (code)
    {
        send(emit, ctx, DOOM_EVENT_CHEAT, code, p->weapon, 0, 0, 0);
        p->cheat_len = 0;
    }
}
DoomDrop doom_enemy_killed(DoomPlayer *p)
{
    int i, best = 0;
    if (!p)
        return DOOM_DROP_BULLETS;
    ++p->kills;
    if (p->kills % 10 == 0 && p->armor < 100)
        return DOOM_DROP_GREEN_ARMOR;
    if (p->kills % 5 == 0 && p->health < 100)
        return DOOM_DROP_MEDIKIT;
    for (i = 1; i < 4; i++)
        if (p->ammo[i] * p->max_ammo[best] < p->ammo[best] * p->max_ammo[i])
            best = i;
    return (DoomDrop)best;
}
int doom_collect(DoomPlayer *p, DoomDrop drop)
{
    int n;
    if (!p || drop < DOOM_DROP_BULLETS || drop > DOOM_DROP_GREEN_ARMOR)
        return 0;
    if (drop <= DOOM_DROP_CELLS)
    {
        n = (int)drop;
        if (p->ammo[n] >= p->max_ammo[n])
            return 0;
        p->ammo[n] += amount[n];
        if (p->ammo[n] > p->max_ammo[n])
            p->ammo[n] = p->max_ammo[n];
        return 1;
    }
    if (drop == DOOM_DROP_MEDIKIT)
    {
        if (p->health >= 100)
            return 0;
        p->health += 25;
        if (p->health > 100)
            p->health = 100;
        return 1;
    }
    if (p->armor >= 100)
        return 0;
    p->armor = 100;
    p->armor_type = 1;
    return 1;
}
