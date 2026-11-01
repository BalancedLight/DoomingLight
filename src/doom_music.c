#include "doom_music.h"
#include <limits.h>
#include <string.h>

static uint16_t read16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

size_t doom_music_parse_mus(const uint8_t *data, size_t size,
                            DoomMusicEvent *out, size_t capacity) {
    static const uint8_t controller[] = {0, 0, 1, 7, 10, 11, 91, 93, 64, 67,
                                         120, 123, 126, 127, 121};
    uint8_t velocity[16];
    size_t pos, end, count = 0;
    uint32_t tick = 0;
    if (!data || size < 16 || memcmp(data, "MUS\x1a", 4)) return 0;
    pos = read16(data + 6);
    if (pos > size || read16(data + 4) > size - pos) return 0;
    end = pos + read16(data + 4);
    memset(velocity, 64, sizeof velocity);
    while (pos < end) {
        uint8_t descriptor = data[pos++], type = (descriptor >> 4) & 7;
        uint8_t channel = descriptor & 15, status = 0, a = 0, b = 0;
        uint8_t midi_channel = channel == 15 ? 9 : channel >= 9 ? channel + 1 : channel;
        if (type == 6) return count;
        if (type == 0 || type == 1 || type == 2 || type == 3 || type == 4) {
            if (pos >= end) return 0;
            a = data[pos++];
            if (type == 0) { status = 0x80; a &= 127; }
            else if (type == 1) {
                status = 0x90;
                if (a & 128) {
                    if (pos >= end || data[pos] > 127) return 0;
                    velocity[channel] = data[pos++];
                }
                a &= 127; b = velocity[channel];
            } else if (type == 2) {
                status = 0xe0; b = a >> 1; a = (uint8_t)((a & 1) << 6);
            } else if (type == 3) {
                if (a < 10 || a > 14) return 0;
                status = 0xb0; a = controller[a]; b = a == 126 ? 1 : 0;
            } else {
                if (a > 9 || pos >= end || data[pos] > 127) return 0;
                b = data[pos++];
                if (!a) { status = 0xc0; a = b; b = 0; }
                else { status = 0xb0; a = controller[a]; }
            }
            if (out) {
                if (count >= capacity) return 0;
                out[count].tick = tick; out[count].status = status | midi_channel;
                out[count].data1 = a; out[count].data2 = b;
            }
            ++count;
        } else return 0;
        if (descriptor & 128) {
            uint32_t delay = 0; uint8_t v; unsigned n = 0;
            do {
                if (pos >= end || ++n > 4 || delay > (UINT_MAX >> 7)) return 0;
                v = data[pos++]; delay = (delay << 7) | (v & 127);
            } while (v & 128);
            if (tick > UINT_MAX - delay) return 0;
            tick += delay;
        }
    }
    return 0;
}

unsigned doom_music_setting_code(int doom2, unsigned track) {
    if (doom2) return track >= 1u && track <= 35u ? track : 1u;
    if (track >= 1u && track <= 9u) return track + 10u;
    if (track == 10u) return 21u;
    if (track >= 11u && track <= 45u && track % 10u != 0) return track;
    return 11u;
}

int doom_music_lump_name(int doom2, unsigned code, char name[9]) {
    static const char *const commercial[] = {
      "RUNNIN", "STALKS", "COUNTD", "BETWEE", "DOOM", "THE_DA", "SHAWN",
      "DDTBLU", "IN_CIT", "DEAD", "STLKS2", "THEDA2", "DOOM2", "DDTBL2",
      "RUNNI2", "DEAD2", "STLKS3", "ROMERO", "SHAWN2", "MESSAG", "COUNT2",
      "DDTBL3", "AMPIE", "THEDA3", "ADRIAN", "MESSG2", "ROMER2", "TENSE",
      "SHAWN3", "OPENIN", "EVIL", "ULTIMA", "READ_M", "DM2TTL", "DM2INT"};
    static const char *const extra[] = {"INTER", "INTRO", "BUNNY", "VICTOR", "INTROA"};
    unsigned index;
    if (!name) return 0;
    memset(name, 0, 9); name[0] = 'D'; name[1] = '_';
    if (doom2) {
        if (code < 1 || code > 35) return 0;
        memcpy(name + 2, commercial[code - 1], strlen(commercial[code - 1]));
        return 1;
    }
    if (code / 10 < 1 || code % 10 < 1 || code % 10 > 9) return 0;
    index = (code / 10 - 1) * 9 + code % 10 - 1;
    if (index > 31) return 0;
    if (index < 27) {
        name[2] = 'E'; name[3] = (char)('1' + index / 9);
        name[4] = 'M'; name[5] = (char)('1' + index % 9);
    } else memcpy(name + 2, extra[index - 27], strlen(extra[index - 27]));
    return 1;
}
