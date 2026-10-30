/* Shared Doom HUD messages. GPL-2.0-or-later. */
#include "doom_messages.h"
#include <string.h>

void doom_messages_reset(DoomMessages *messages)
{
    if (messages)
        memset(messages, 0, sizeof(*messages));
}

void doom_messages_tick(DoomMessages *messages, unsigned elapsed_ms)
{
    if (!messages)
        return;
    messages->remaining_ms = elapsed_ms >= messages->remaining_ms
        ? 0 : messages->remaining_ms - elapsed_ms;
}

int doom_messages_post(DoomMessages *messages, const char *text,
                       unsigned priority, unsigned duration_ms)
{
    unsigned length = 0;
    if (!messages || !text || !duration_ms || duration_ms > 60000u ||
        priority > DOOM_MESSAGE_IMPORTANT ||
        (messages->remaining_ms && priority < messages->priority))
        return 0;
    while (length < sizeof(messages->text) && text[length])
    {
        unsigned char ch = (unsigned char)text[length];
        if (ch < 32u || ch > 126u)
            return 0;
        ++length;
    }
    if (!length || length >= sizeof(messages->text))
        return 0;
    memmove(messages->text, text, length);
    messages->text[length] = '\0';
    messages->remaining_ms = duration_ms;
    messages->priority = priority;
    return 1;
}

const char *doom_messages_text(const DoomMessages *messages)
{
    return messages && messages->remaining_ms ? messages->text : 0;
}
