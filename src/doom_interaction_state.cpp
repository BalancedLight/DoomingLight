#include "doom_interaction_state.h"

#if defined(DOOM_MODE_RUNTIME)
#include <windows.h>

extern "C" int doom_interaction_state(void *game_module, void *actor)
{
    if (!game_module || !actor || game_module != GetModuleHandleW(L"gamedll_x64_rwdi.dll")) return -1;
    __try {
        auto *game = static_cast<unsigned char *>(game_module);
        auto *player = static_cast<unsigned char *>(actor);
        void *global = *reinterpret_cast<void **>(game + 0x1d07f88);
        using GetPlayer = void *(__fastcall *)(void *);
        if (!global || *reinterpret_cast<void **>(global) != game + 0x157d5f8 ||
            reinterpret_cast<GetPlayer>(game + 0x3c2d90)(global) != actor ||
            *reinterpret_cast<void **>(actor) != game + 0x17dd738) return -1;
        auto *controller = *reinterpret_cast<unsigned char **>(player + 0xc88);
        if (!controller) return -1;
        auto **table = *reinterpret_cast<void ***>(controller);
        if (table != reinterpret_cast<void **>(game + 0x1809c78) ||
            table[4] != game + 0x106b80 || table[18] != game + 0xcbcd30 ||
            table[19] != game + 0xcbd110 ||
            *reinterpret_cast<void **>(controller + 8) != actor ||
            *reinterpret_cast<int *>(controller + 20) != 25) return -1;
        if (!controller[16] || *reinterpret_cast<int *>(controller + 0xd0) == 4) return 0;
        if (!*reinterpret_cast<void **>(controller + 72) ||
            !*reinterpret_cast<void **>(controller + 80)) return 0;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
#else
extern "C" int doom_interaction_state(void *, void *) { return -1; }
#endif
