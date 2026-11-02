#include "doom_music_selection.h"
#include <cassert>
int main() {
    const DoomMusicChoice choices[] = {{1, false}, {1, true}, {2, true}};
    assert(doom_music_eligible_count(choices, 3, {1, false}) == 2);
    assert(doom_music_choice_equal(doom_music_choose(choices, 3, {1, false}, 0), {1, true}));
    assert(doom_music_choice_equal(doom_music_choose(choices, 3, {1, false}, 1), {2, true}));
    assert(doom_music_eligible_count(choices, 1, {1, false}) == 1);
    assert(doom_music_choice_equal(doom_music_choose(choices, 1, {1, false}, 0), {1, false}));
    assert(doom_music_eligible_count(nullptr, 0, {}) == 0);
    assert(doom_music_choose(choices, 3, {}, 3).track == 0);
}
