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
    doom_messages_tick(&feedback->messages, elapsed_ms);
}
void doom_pickup_feedback_record(DoomPickupFeedback *feedback, DoomDrop kind, int amount)
{
    static const char *const names[] = {"bullets", "shells", "rockets", "cells", "health", "armor"};
    static const char *const powers[] = {"Invulnerability!", "Berserk!", "Partial Invisibility",
        "Radiation Shielding Suit", "Computer Area Map", "Light Amplification Visor"};
    char text[64];
    if (!feedback || kind < DOOM_DROP_BULLETS || kind >= DOOM_DROP_COUNT || amount <= 0)
        return;
    if (kind >= DOOM_DROP_INVULNERABILITY) {
        feedback->kind = kind;
        feedback->amount = 1;
        feedback->recorded_this_frame = 1;
        (void)doom_messages_post(&feedback->messages, powers[kind - DOOM_DROP_INVULNERABILITY],
                                 DOOM_MESSAGE_NORMAL, PICKUP_MESSAGE_MS);
        return;
    }
    if (feedback->recorded_this_frame && feedback->kind == kind)
        feedback->amount = amount > INT_MAX - feedback->amount ? INT_MAX : feedback->amount + amount;
    else
        feedback->amount = amount;
    feedback->kind = kind;
    feedback->recorded_this_frame = 1;
    snprintf(text, sizeof(text), "You picked up %s +%d", names[(int)kind], feedback->amount);
    (void)doom_messages_post(&feedback->messages, text, DOOM_MESSAGE_NORMAL, PICKUP_MESSAGE_MS);
}
const char *doom_pickup_feedback_text(const DoomPickupFeedback *feedback)
{
    return feedback ? doom_messages_text(&feedback->messages) : 0;
}
