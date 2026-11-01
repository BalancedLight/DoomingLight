#include "doom_music.h"
#include <assert.h>
#include <string.h>

int main(void) {
    char name[9];
    unsigned code;
    assert(doom_music_setting_code(1, 0) == 1);
    assert(doom_music_setting_code(1, 1) == 1);
    assert(doom_music_setting_code(1, 2) == 2);
    assert(doom_music_lump_name(1, 1, name) && !strcmp(name, "D_RUNNIN"));
    assert(doom_music_lump_name(1, 2, name) && !strcmp(name, "D_STALKS"));
    assert(doom_music_setting_code(0, 0) == 11);
    assert(doom_music_setting_code(0, 1) == 11);
    assert(doom_music_setting_code(0, 2) == 12);
    for (code = 11; code <= 45; ++code)
        if (code % 10) assert(doom_music_setting_code(0, code) == code);
    assert(doom_music_lump_name(0, 11, name) && !strcmp(name, "D_E1M1"));
    assert(doom_music_lump_name(0, 12, name) && !strcmp(name, "D_E1M2"));
    return 0;
}
