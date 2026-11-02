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
/* The token identifies the current FIFO head; appending does not invalidate it. */
int doom_loot_adapter_peek(DoomLootAdapter *adapter, DoomLootRecord *out, uint64_t *token);
int doom_loot_adapter_consume(DoomLootAdapter *adapter, uint64_t token);
/* Defer an uncollectible head so another reward kind can be delivered. */
int doom_loot_adapter_rotate(DoomLootAdapter *adapter, uint64_t token);
int doom_loot_adapter_stop(DoomLootAdapter *adapter);
void doom_loot_adapter_destroy(DoomLootAdapter *adapter);
#ifdef __cplusplus
}
#endif
#endif
