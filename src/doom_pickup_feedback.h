#ifndef DOOM_PICKUP_FEEDBACK_H
#define DOOM_PICKUP_FEEDBACK_H
#include "doom_core.h"
#include "doom_messages.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomPickupFeedback
{
    DoomMessages messages;
    DoomDrop kind;
    int amount, recorded_this_frame;
} DoomPickupFeedback;

void doom_pickup_feedback_reset(DoomPickupFeedback *feedback);
/* Call once before processing each frame's pickups. */
void doom_pickup_feedback_begin_frame(DoomPickupFeedback *feedback, unsigned elapsed_ms);
void doom_pickup_feedback_record(DoomPickupFeedback *feedback, DoomDrop kind, int amount);
const char *doom_pickup_feedback_text(const DoomPickupFeedback *feedback);

#ifdef __cplusplus
}
#endif
#endif
