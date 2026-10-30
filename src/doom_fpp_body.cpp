#include "doom_fpp_body.h"
#include <windows.h>
#include <cstring>

namespace {
using GetPlayer = void *(__fastcall *)(void *);
using Scene = void (__fastcall *)(void *, unsigned char);
using ThreadIndex = unsigned (__fastcall *)();
struct State {
    unsigned char *game, *engine;
    Scene scene;
    ThreadIndex thread_index;
    void *player, *visual, *object;
    bool owned;
};
State state{};

bool current(void *&player, void *&visual, void *&object) {
    void *game = *reinterpret_cast<void **>(state.game + 0x1d07f88);
    player = game ? reinterpret_cast<GetPlayer>(state.game + 0x3c2d90)(game) : nullptr;
    if (!player) return false;
    auto **player_table = *reinterpret_cast<void ***>(player);
    if (player_table != reinterpret_cast<void **>(state.game + 0x17dd738) ||
        player_table[194] != state.game + 0x106b90) return false;
    visual = reinterpret_cast<GetPlayer>(player_table[194])(player);
    if (!visual || *reinterpret_cast<void **>(visual) != state.game + 0x17e1188 ||
        *reinterpret_cast<void **>(static_cast<unsigned char *>(visual) + 80) != player) return false;
    object = *reinterpret_cast<void **>(static_cast<unsigned char *>(player) + 8);
    if (!object) return false;
    auto **table = *reinterpret_cast<void ***>(object);
    return table && table[160] == state.engine + 0x133ae0;
}
bool scene_enabled(void *object) {
    const unsigned offset = state.thread_index() < 2 ? 748u : 812u;
    return (*reinterpret_cast<const unsigned char *>(static_cast<unsigned char *>(object) + offset) & 1u) != 0;
}
void forget() { state.player = nullptr; state.visual = nullptr; state.object = nullptr; state.owned = false; }
}
extern "C" int doom_fpp_body_start(void *game_module, void *engine_module) {
    auto *game = static_cast<unsigned char *>(game_module);
    auto *engine = static_cast<unsigned char *>(engine_module);
    static const unsigned char player_bytes[] = {0x48,0x8b,0x81,0x08,0x05,0x00,0x00,0x48,0x85,0xc0,0x74,0x18};
    static const unsigned char visual_bytes[] = {0x48,0x81,0xc1,0x40,0x03,0x00,0x00,0x48,0x8b,0x01,0x48,0xff,0x60,0x08};
    static const unsigned char scene_bytes[] = {0x48,0x83,0xec,0x28,0x48,0x83,0x79,0x08,0x00,0x74,0x0d};
    static const unsigned char scene_impl[] = {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0x99,0xa0,0x02,0x00,0x00};
    static const unsigned char getter_bytes[] = {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x59,0x08,0x48,0x85,0xdb,0x74,0x36};
    static const unsigned char thread_bytes[] = {0x40,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x05};
    if (!game || !engine || (state.game && (state.game != game || state.engine != engine))) return 0;
    __try {
        if (std::memcmp(game+0x3c2d90,player_bytes,sizeof player_bytes) ||
            std::memcmp(game+0x106b90,visual_bytes,sizeof visual_bytes) ||
            std::memcmp(engine+0x14ea90,scene_bytes,sizeof scene_bytes) ||
            std::memcmp(engine+0x133ae0,scene_impl,sizeof scene_impl) ||
            std::memcmp(engine+0x14aa80,getter_bytes,sizeof getter_bytes) ||
            std::memcmp(engine+0x18590,thread_bytes,sizeof thread_bytes)) return 0;
        auto scene = reinterpret_cast<Scene>(GetProcAddress(reinterpret_cast<HMODULE>(engine),
                                      "?EnableRenderingScene@IModelObject@@QEAAX_N@Z"));
        if (reinterpret_cast<void *>(scene) != engine+0x14ea90) return 0;
        state.game=game; state.engine=engine; state.scene=scene;
        state.thread_index=reinterpret_cast<ThreadIndex>(engine+0x18590);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" int doom_fpp_body_update(int active) {
    if (!state.game || !state.engine) return 0;
    __try {
        void *player=nullptr, *visual=nullptr, *object=nullptr;
        if (!current(player,visual,object)) return 0;
        if (player != state.player || visual != state.visual || object != state.object) {
            forget(); state.player=player; state.visual=visual; state.object=object;
        }
        const bool shown=scene_enabled(object);
        if (active) {
            if (shown) { state.scene(player,0); state.owned=true; }
        } else if (state.owned) {
            if (!shown) state.scene(player,1);
            state.owned=false;
        }
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" int doom_fpp_body_stop(void) {
    if (!state.game) return 1;
    if (state.owned && !doom_fpp_body_update(0)) return 0;
    state=State{};
    return 1;
}
