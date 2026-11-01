#ifndef DOOM_MENU_GUARD_H
#define DOOM_MENU_GUARD_H

#ifdef __cplusplus
extern "C" {
#endif

/* CBC7 Player action IDs. Interact, traversal and pause remain available. */
static inline int doom_menu_guard_blocks_action(unsigned int id)
{
    switch (id) {
    case 8: case 70: case 122: case 131: case 253: /* Player menus */
    case 60: case 63: /* Primary and secondary quick-selection HUD */
    case 18:  /* Weapon throw */
    case 35:  /* Ground pound */
    case 68:  /* Attack */
    case 69:  /* Charge attack */
    case 139: /* Weapon zoom */
    case 159: /* Attack trigger right */
    case 160: /* Attack trigger left */
    case 161: /* Fire weapon */
    case 167: /* Kick */
    case 170: /* Melee attack */
    case 218: /* Use equipped secondary */
    case 227: /* Charge attack left */
    case 276: /* Weapon aim toggle */
    case 284: /* Tackle */
        return 1;
    default:
        return 0;
    }
}

/* The caller selects the independently validated Player profile. */
int doom_menu_guard_start(void *game_module, int supported_profile);
int doom_menu_guard_stop(void);

#ifdef __cplusplus
}
#endif
#endif
