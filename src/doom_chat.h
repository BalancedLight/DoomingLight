#ifndef DOOM_CHAT_H
#define DOOM_CHAT_H
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DOOM_CHAT_HISTORY 4
#define DOOM_CHAT_TEXT_CAPACITY 1024
typedef struct DoomChatSnapshot {
    int available, open, channel;
    unsigned owner_thread, polling_thread;
    int validation;
    unsigned notifications, line_count;
    char lines[DOOM_CHAT_HISTORY][DOOM_CHAT_TEXT_CAPACITY];
    char draft[DOOM_CHAT_TEXT_CAPACITY];
} DoomChatSnapshot;

/* The caller supplies the exact supported Player profile and owned map. */
int doom_chat_start(void *game, void *engine, void *level, uint32_t generation, int supported);
int doom_chat_typing(void);
void doom_chat_poll(DoomChatSnapshot *out);
/* Hide native presentation only after the replacement was presented. */
void doom_chat_presented(int ready);
void doom_chat_stop(void);

#ifdef __cplusplus
}
#endif
#endif
