#ifndef DOOM_MUSIC_SELECTION_H
#define DOOM_MUSIC_SELECTION_H

#include <cstddef>
#include <cstdint>

struct DoomMusicChoice {
    unsigned track = 0;
    bool doom2 = false;
};

inline bool doom_music_choice_equal(DoomMusicChoice a, DoomMusicChoice b) {
    return a.track == b.track && a.doom2 == b.doom2;
}

/* The caller supplies a uniformly chosen index in [0, eligible_count). */
inline std::size_t doom_music_eligible_count(const DoomMusicChoice* choices,
                                             std::size_t count,
                                             DoomMusicChoice previous) {
    if (!choices || !count) return 0;
    for (std::size_t i = 0; i < count; ++i)
        if (doom_music_choice_equal(choices[i], previous)) return count - (count > 1 ? 1 : 0);
    return count;
}

inline DoomMusicChoice doom_music_choose(const DoomMusicChoice* choices,
                                         std::size_t count,
                                         DoomMusicChoice previous,
                                         std::size_t index) {
    const std::size_t eligible = doom_music_eligible_count(choices, count, previous);
    if (!eligible || index >= eligible) return {};
    for (std::size_t i = 0; i < count; ++i) {
        if (count > 1 && doom_music_choice_equal(choices[i], previous)) continue;
        if (!index--) return choices[i];
    }
    return {};
}
#endif
