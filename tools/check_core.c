/* Focused checks for Doom Mode's player-state contracts. */
#include "doom_core.h"
#include <assert.h>

static int attacks, spread_seen;
static void collect_event(void *context, const DoomEvent *event)
{
    (void)context;
    if (event->type == DOOM_EVENT_ATTACK)
    {
        ++attacks;
        if (event->vertical_spread != 0)
            spread_seen = 1;
    }
}
static void cheat(DoomPlayer *player, const char *text)
{
    for (; *text; ++text)
        doom_cheat_char(player, *text, collect_event, 0);
}
int main(void)
{
    DoomPlayer player;
    unsigned i;
    doom_reset(&player, 0);
    assert(player.health == 100 && player.armor == 0 && player.ammo[0] == 200);
    assert(!(player.owned_mask & (1u << DOOM_SUPER_SHOTGUN)));
    doom_advance(&player, 200, 1, collect_event, 0);
    assert(attacks == 1 && player.ammo[DOOM_BULLETS] == 199);
    player.armor = 100;
    player.armor_type = 1;
    assert(doom_damage(&player, 30) == 20 && player.armor == 90);
    cheat(&player, "IDDQD");
    assert(player.god && doom_damage(&player, 50) == 0);
    cheat(&player, "IDFA");
    assert(player.ammo[0] == 200 && player.max_ammo[0] == 200);

    doom_reset(&player, 1);
    assert(player.owned_mask & (1u << DOOM_SUPER_SHOTGUN));
    doom_select_slot(&player, 1);
    assert(player.pending_weapon == DOOM_CHAINSAW);
    player.weapon = DOOM_CHAINSAW;
    doom_select_slot(&player, 1);
    assert(player.pending_weapon == DOOM_FIST);
    player.weapon = DOOM_SHOTGUN;
    doom_select_slot(&player, 3);
    assert(player.pending_weapon == DOOM_SUPER_SHOTGUN);
    player.weapon = DOOM_SUPER_SHOTGUN;
    doom_select_slot(&player, 3);
    assert(player.pending_weapon == DOOM_SHOTGUN);
    doom_reset(&player, 1);
    player.weapon = DOOM_CHAINGUN;
    attacks = 0;
    doom_advance(&player, 29, 1, collect_event, 0);
    assert(attacks == 1 && player.ammo[0] == 199);
    assert(player.flash_weapon == DOOM_CHAINGUN && player.flash_tics == 5);

    doom_reset(&player, 1);
    player.weapon = DOOM_SUPER_SHOTGUN;
    attacks = spread_seen = 0;
    for (i = 0; i < 5; ++i)
        doom_advance(&player, 29, 1, collect_event, 0);
    assert(attacks == 20 && spread_seen && player.ammo[DOOM_SHELLS] == 48);
    assert(player.flash_weapon == DOOM_SUPER_SHOTGUN);

    doom_reset(&player, 1);
    player.weapon = DOOM_PLASMA;
    attacks = 0;
    doom_advance(&player, 200, 1, collect_event, 0);
    assert(attacks == 3 && player.ammo[DOOM_CELLS] == 297);
    doom_advance(&player, 100, 0, collect_event, 0);
    assert(player.plasma_recovery);

    doom_reset(&player, 1);
    player.kills = 4;
    player.health = 70;
    assert(doom_enemy_killed(&player) == DOOM_DROP_MEDIKIT);
    assert(doom_collect(&player, DOOM_DROP_MEDIKIT) && player.health == 95);
    player.kills = 9;
    assert(doom_enemy_killed(&player) == DOOM_DROP_GREEN_ARMOR);
    assert(doom_collect(&player, DOOM_DROP_GREEN_ARMOR) && player.armor == 100);
    doom_reset(&player, 1);
    player.health = 0;
    player.flash_tics = 4;
    player.cooldown = 10;
    attacks = 0;
    for (i = 0; i < 16; ++i)
        doom_tick(&player, 1, collect_event, 0);
    assert(player.face_index == 41 && player.weapon_y == 128 && player.view_height == 25 && !player.flash_tics);
    assert(!attacks && !player.cooldown && player.pending_weapon == -1);
    for (i = 0; i < 25; ++i)
        doom_tick(&player, 0, collect_event, 0);
    assert(player.view_height == 6);
    return 0;
}
