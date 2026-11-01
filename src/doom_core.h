#ifndef DOOM_CORE_H
#define DOOM_CORE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif

    typedef enum DoomWeapon
    {
        DOOM_FIST,
        DOOM_PISTOL,
        DOOM_SHOTGUN,
        DOOM_CHAINGUN,
        DOOM_ROCKET,
        DOOM_PLASMA,
        DOOM_BFG,
        DOOM_CHAINSAW,
        DOOM_SUPER_SHOTGUN,
        DOOM_WEAPON_COUNT
    } DoomWeapon;
    typedef enum DoomAmmo
    {
        DOOM_BULLETS,
        DOOM_SHELLS,
        DOOM_ROCKETS,
        DOOM_CELLS,
        DOOM_AMMO_COUNT,
        DOOM_NO_AMMO = -1
    } DoomAmmo;
    typedef enum DoomEventType
    {
        DOOM_EVENT_ATTACK,
        DOOM_EVENT_MUSIC,
        DOOM_EVENT_CHEAT,
        DOOM_EVENT_DROPPED,
        DOOM_EVENT_SOUND,
        /* Once per weapon cycle after its attack callbacks. */
        DOOM_EVENT_WEAPON_FIRED
    } DoomEventType;
    typedef enum DoomCheatId
    {
        DOOM_CHEAT_GOD = 1,
        DOOM_CHEAT_AMMO = 2,
        DOOM_CHEAT_CHOPPERS = 3,
        DOOM_CHEAT_AMMO_KEYS = 4,
        DOOM_CHEAT_NOCLIP = 5
    } DoomCheatId;
    typedef enum DoomSoundId
    {
        DOOM_SOUND_PISTOL,
        DOOM_SOUND_SHOTGN,
        DOOM_SOUND_SGCOCK,
        DOOM_SOUND_DSHTGN,
        DOOM_SOUND_DBOPN,
        DOOM_SOUND_DBLOAD,
        DOOM_SOUND_DBCLS,
        DOOM_SOUND_SAWUP,
        DOOM_SOUND_SAWIDL,
        DOOM_SOUND_SAWFUL,
        DOOM_SOUND_RLAUNC,
        DOOM_SOUND_PLASMA,
        DOOM_SOUND_BFG
    } DoomSoundId;
    typedef enum DoomDrop
    {
        DOOM_DROP_BULLETS,
        DOOM_DROP_SHELLS,
        DOOM_DROP_ROCKETS,
        DOOM_DROP_CELLS,
        DOOM_DROP_MEDIKIT,
        DOOM_DROP_GREEN_ARMOR
    } DoomDrop;
    typedef struct DoomEvent
    {
        DoomEventType type;
        int value;
        int weapon;
        int pellets;
        int damage;
        /* Horizontal deviation is the Doom random difference, doubled for
           super shotgun; vertical deviation is the undoubled difference.
           The host converts these to Doom's angle/slope units. */
        int spread;
        int vertical_spread;
    } DoomEvent;
    typedef void (*DoomEventFn)(void *context, const DoomEvent *event);
    typedef struct DoomPlayer
    {
        int doom2, health, armor, armor_type, god, keys, weapon, pending_weapon;
        int ammo[DOOM_AMMO_COUNT], max_ammo[DOOM_AMMO_COUNT];
        unsigned owned_mask, kills, random_index, accumulator, cooldown, fire_delay, attack_down,
            refire;
        unsigned face_tics, face_random, shot_fired, invulnerability_tics, switch_phase,
            plasma_recovery, flash_tics, saw_idle_tics, face_attack_tics;
        unsigned damage_count;
        int face, face_index, weapon_frame, weapon_y, view_height, flash_weapon, flash_frame;
        char cheat[16];
        unsigned cheat_len;
    } DoomPlayer;
    void doom_reset(DoomPlayer *player, int doom2);
    unsigned doom_random(DoomPlayer *player);
    void doom_select_weapon(DoomPlayer *player, DoomWeapon weapon);
    void doom_select_slot(DoomPlayer *player, int slot);
    /* Cancel attack actions without changing ammo, inventory, weapon switch,
     * death, or face state. Continue passing fire=0 while attacks are blocked. */
    void doom_cancel_attack(DoomPlayer *player);
    /* Advance exactly one 35 Hz tic, for hosts synchronizing world effects. */
    void doom_tick(DoomPlayer *player, int fire, DoomEventFn emit, void *context);
    void doom_advance(DoomPlayer *player, unsigned elapsed_ms, int fire, DoomEventFn emit,
                      void *context);
    int doom_damage(DoomPlayer *player, int damage);
    int doom_damage_from(DoomPlayer *player, int damage, int attacker_side);
    unsigned doom_damage_palette(const DoomPlayer *player);
    /* True if candidate is a case-insensitive prefix of a supported cheat,
     * including up to two IDMUS digits. Caller owns timeout and buffering. */
    int doom_cheat_is_prefix(const char *candidate, unsigned length);
    /* True while IDMUS is waiting for either track-number digit. */
    int doom_cheat_wants_music_digit(const DoomPlayer *player);
    void doom_cheat_char(DoomPlayer *player, char ch, DoomEventFn emit, void *context);
    DoomDrop doom_enemy_killed(DoomPlayer *player);
    int doom_collect(DoomPlayer *player, DoomDrop drop);
#ifdef __cplusplus
}
#endif
#endif
