/* Focused checks for Doom Mode's player-state contracts. */
#include "doom_core.h"
#include <assert.h>

static int attacks, spread_seen, music_count, music_track;
static void collect_event(void *context, const DoomEvent *event)
{
    (void)context;
    if (event->type == DOOM_EVENT_ATTACK)
    {
        ++attacks;
        if (event->vertical_spread != 0)
            spread_seen = 1;
    }
    else if (event->type == DOOM_EVENT_MUSIC)
    {
        ++music_count;
        music_track = event->value;
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
    cheat(&player, "IDMUS");
    assert(doom_cheat_wants_music_digit(&player));
    cheat(&player, "3");
    assert(doom_cheat_wants_music_digit(&player));
    cheat(&player, "5");
    assert(!doom_cheat_wants_music_digit(&player) && music_count == 1 && music_track == 35);
    cheat(&player, "IDMUSX");
    assert(!doom_cheat_wants_music_digit(&player));
    cheat(&player, "12");
    assert(music_count == 1);

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

    /* The chainsaw's idle C/D frames and attack A/B frames each last four tics. */
    doom_reset(&player, 1);
    player.weapon = DOOM_CHAINSAW;
    attacks = 0;
    for (i = 0; i < 4; ++i) { doom_tick(&player, 0, collect_event, 0); assert(player.weapon_frame == 2); }
    for (i = 0; i < 4; ++i) { doom_tick(&player, 0, collect_event, 0); assert(player.weapon_frame == 3); }
    doom_tick(&player, 1, collect_event, 0);
    assert(player.weapon_frame == 0 && attacks == 1);
    for (i = 0; i < 3; ++i) { doom_tick(&player, 1, collect_event, 0); assert(player.weapon_frame == 0); }
    doom_tick(&player, 1, collect_event, 0);
    assert(player.weapon_frame == 1 && attacks == 2);
    for (i = 0; i < 3; ++i) { doom_tick(&player, 0, collect_event, 0); assert(player.weapon_frame == 1); }
    doom_tick(&player, 0, collect_event, 0);
    assert(player.weapon_frame == 2);
    doom_reset(&player, 1);
    attacks = 0;
    doom_select_weapon(&player, DOOM_CHAINSAW);
    for (i = 0; i < 40; ++i) doom_tick(&player, 0, collect_event, 0);
    assert(player.weapon == DOOM_CHAINSAW && (player.weapon_frame == 2 || player.weapon_frame == 3));
    assert(attacks == 0);

    /* Idle faces never insert an extra straight frame; sustained fire ramps
       up after two seconds and damage expressions retain priority. */
    doom_reset(&player, 1);
    player.face_index = 2; player.face_tics = 1;
    doom_tick(&player, 0, collect_event, 0);
    assert(player.face_tics == 17 && player.face_index >= 0 && player.face_index <= 2);
    for (i = 0; i < 70; ++i) doom_tick(&player, 1, collect_event, 0);
    assert(player.face_index != 7);
    doom_tick(&player, 1, collect_event, 0);
    assert(player.face_index == 7);
    doom_damage_from(&player, 5, -1);
    assert(player.face_index == 4);
    doom_tick(&player, 1, collect_event, 0);
    assert(player.face_index == 4);
    for (i = 0; i < 34; ++i) doom_tick(&player, 1, collect_event, 0);
    assert(player.face_index == 7);
    doom_tick(&player, 0, collect_event, 0);
    assert(player.face_index < 3);

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
    player.weapon = DOOM_BFG;
    for (i = 0; i < 32; ++i) doom_tick(&player, 1, collect_event, 0);
    assert(player.ammo[DOOM_CELLS] == 260 && player.ammo[DOOM_ROCKETS] == 50);
    doom_reset(&player, 1);
    player.weapon = DOOM_ROCKET;
    for (i = 0; i < 10; ++i) doom_tick(&player, 1, collect_event, 0);
    assert(player.ammo[DOOM_CELLS] == 300 && player.ammo[DOOM_ROCKETS] == 49);

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
