/* Doom Mode pickup messages. GPL-2.0-or-later. */
#include "doom_pickup_feedback.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define PICKUP_MESSAGE_MS 3000u

void doom_pickup_feedback_reset(DoomPickupFeedback *feedback)
{
    if (feedback)
        memset(feedback, 0, sizeof(*feedback));
}
void doom_pickup_feedback_begin_frame(DoomPickupFeedback *feedback, unsigned elapsed_ms)
{
    if (!feedback)
        return;
    feedback->recorded_this_frame = 0;
    feedback->remaining_ms = elapsed_ms >= feedback->remaining_ms ? 0 : feedback->remaining_ms - elapsed_ms;
}
void doom_pickup_feedback_record(DoomPickupFeedback *feedback, DoomDrop kind, int amount)
{
    static const char *const names[] = {"bullets", "shells", "rockets", "cells", "health", "armor"};
    if (!feedback || kind < DOOM_DROP_BULLETS || kind > DOOM_DROP_GREEN_ARMOR || amount <= 0)
        return;
    if (feedback->recorded_this_frame && feedback->kind == kind)
        feedback->amount = amount > INT_MAX - feedback->amount ? INT_MAX : feedback->amount + amount;
    else
        feedback->amount = amount;
    feedback->kind = kind;
    feedback->recorded_this_frame = 1;
    feedback->remaining_ms = PICKUP_MESSAGE_MS;
    snprintf(feedback->text, sizeof(feedback->text), "Picked up %s +%d", names[(int)kind], feedback->amount);
}
const char *doom_pickup_feedback_text(const DoomPickupFeedback *feedback)
{
    return feedback && feedback->remaining_ms ? feedback->text : 0;
}
