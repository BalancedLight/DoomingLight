#ifndef DOOM_LOOT_ADAPTER_H
#define DOOM_LOOT_ADAPTER_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct DoomLootAdapter DoomLootAdapter;
typedef enum DoomLootKind { DOOM_LOOT_AMMO = 1, DOOM_LOOT_HEALTH = 2 } DoomLootKind;
typedef struct DoomLootRecord { uint32_t generation; DoomLootKind kind; } DoomLootRecord;
/* Caller verifies the exact supported Player profile before creating this adapter. */
DoomLootAdapter *doom_loot_adapter_create(void *game_module);
int doom_loot_adapter_start(DoomLootAdapter *adapter);
/* Keep ownership across pauses; accepting controls new conversions. */
void doom_loot_adapter_context(DoomLootAdapter *adapter, void *player, void *level, uint32_t generation, int accepting);
unsigned doom_loot_adapter_drain(DoomLootAdapter *adapter, DoomLootRecord *out, unsigned capacity);
int doom_loot_adapter_stop(DoomLootAdapter *adapter);
void doom_loot_adapter_destroy(DoomLootAdapter *adapter);
#ifdef __cplusplus
}
#endif
#endif
