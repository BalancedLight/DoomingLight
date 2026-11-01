#ifndef DOOM_GAMEPAD_H
#define DOOM_GAMEPAD_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomGamepad DoomGamepad;
typedef struct DoomGamepadState { int fire; int cycle; } DoomGamepadState;
DoomGamepad *doom_gamepad_create(void);
void doom_gamepad_reset(DoomGamepad *pad);
/* Reads the first connected XInput controller. When disabled, input is
 * tracked but ignored until buttons are released. */
DoomGamepadState doom_gamepad_sample(DoomGamepad *pad,int enabled,int left_trigger,int reverse_cycle);
void doom_gamepad_destroy(DoomGamepad *pad);
#ifdef __cplusplus
}
#endif
#endif
