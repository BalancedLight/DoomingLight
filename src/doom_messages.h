#ifndef DOOM_MESSAGES_H
#define DOOM_MESSAGES_H

#ifdef __cplusplus
extern "C" {
#endif

enum DoomMessagePriority
{
    DOOM_MESSAGE_NORMAL = 0,
    DOOM_MESSAGE_IMPORTANT = 1
};

typedef struct DoomMessages
{
    char text[128];
    unsigned remaining_ms;
    unsigned priority;
} DoomMessages;

/* One active message, with important messages protected from ordinary updates. */
void doom_messages_reset(DoomMessages *messages);
void doom_messages_tick(DoomMessages *messages, unsigned elapsed_ms);
int doom_messages_post(DoomMessages *messages, const char *text,
                       unsigned priority, unsigned duration_ms);
const char *doom_messages_text(const DoomMessages *messages);

#ifdef __cplusplus
}
#endif
#endif
