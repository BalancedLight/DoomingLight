#include "doom_core.h"
#include "doom_world.h"
#include <assert.h>
#include <string.h>

static int last_cheat, attack_damage;
static void event(void *context, const DoomEvent *e) {
    (void)context;
    if (e->type == DOOM_EVENT_CHEAT) last_cheat = e->value;
    if (e->type == DOOM_EVENT_ATTACK) attack_damage = e->damage;
}
static void type(DoomPlayer *p, const char *s) {
    while (*s) doom_cheat_char(p, *s++, event, 0);
}
int main(void) {
    DoomPlayer p; DoomWorld world; unsigned i; int normal;
    doom_reset(&p, 1);
    assert(doom_cheat_is_prefix("IDBEHOLD", 8));
    assert(doom_cheat_is_prefix("IDBEHOLDV", 9));
    type(&p, "IDBEHOLD"); assert(last_cheat == DOOM_CHEAT_BEHOLD_MENU);
    type(&p, "V"); assert(last_cheat == DOOM_CHEAT_BEHOLD_POWER && p.invulnerability_tics == 1050);
    assert(doom_damage(&p, 30) == 0);
    type(&p, "IDBEHOLDV"); doom_tick(&p, 0, event, 0);
    assert(!doom_power_active(&p, DOOM_POWER_INVULNERABILITY));
    assert(doom_collect(&p, DOOM_DROP_RADIATION_SUIT));
    assert(doom_environment_protected(&p, DOOM_ENVIRONMENT_ACID));
    assert(doom_environment_protected(&p, DOOM_ENVIRONMENT_FIRE));
    assert(!doom_environment_protected(&p, DOOM_ENVIRONMENT_OTHER));
    for (i = 0; i < 2100; ++i) doom_tick(&p, 0, event, 0);
    assert(!doom_power_active(&p, DOOM_POWER_RADIATION_SUIT));
    doom_reset(&p, 1); p.weapon = DOOM_FIST; doom_tick(&p, 1, event, 0); normal = attack_damage;
    doom_reset(&p, 1); p.weapon = DOOM_FIST; p.health = 20;
    assert(doom_collect(&p, DOOM_DROP_BERSERK) && p.health == 100);
    doom_tick(&p, 1, event, 0); assert(attack_damage == normal * 10);
    assert(doom_collect(&p, DOOM_DROP_LIGHT_AMP) && p.power_tics[DOOM_POWER_LIGHT_AMP] == 4200);
    assert(doom_collect(&p, DOOM_DROP_AUTOMAP)); assert(!doom_collect(&p, DOOM_DROP_AUTOMAP));
    type(&p, "IDBEHOLDA"); assert(!doom_power_active(&p, DOOM_POWER_AUTOMAP));
    type(&p, "IDBEHOLDA"); assert(doom_power_active(&p, DOOM_POWER_AUTOMAP));
    doom_reset(&p, 1); for (i = 0; i < DOOM_POWER_COUNT; ++i) assert(!doom_power_active(&p, (DoomPower)i));
    memset(&world, 0, sizeof(world));
    assert(doom_world_spawn_pickup(&world, DOOM_DROP_INVULNERABILITY, (DoomVec3){0,0,0}));
    assert(!strcmp(doom_world_pickup_sprite(DOOM_DROP_INVULNERABILITY), "PINVA0"));
    return 0;
}
