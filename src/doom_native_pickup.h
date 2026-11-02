#ifndef DOOM_NATIVE_PICKUP_H
#define DOOM_NATIVE_PICKUP_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomNativePickup DoomNativePickup;
typedef enum DoomNativePickupKind { DOOM_NATIVE_PICKUP_NORMAL = 0, DOOM_NATIVE_PICKUP_SECRET = 1 } DoomNativePickupKind;
typedef struct DoomNativePickupRecord { char text[128]; DoomNativePickupKind kind; } DoomNativePickupRecord;
/* The caller must first verify the exact supported Player module profile. */
DoomNativePickup *doom_native_pickup_create(void *game_module);
int doom_native_pickup_start(DoomNativePickup *pickup);
/* Enable only while Doom's HUD can present pickup feedback. */
void doom_native_pickup_accept(DoomNativePickup *pickup, int accepting);
unsigned doom_native_pickup_drain(DoomNativePickup *pickup, DoomNativePickupRecord *out, unsigned capacity);
int doom_native_pickup_stop(DoomNativePickup *pickup);
void doom_native_pickup_destroy(DoomNativePickup *pickup);
#ifdef __cplusplus
}
#endif
#endif
