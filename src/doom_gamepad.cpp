#include "doom_gamepad.h"
#include <windows.h>
#include <Xinput.h>
#include <new>

struct DoomGamepad {
    using GetState = DWORD (WINAPI *)(DWORD,XINPUT_STATE *);
    HMODULE module=nullptr;
    GetState get=nullptr;
    DWORD index=0;
    WORD previous=XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT;
    ULONGLONG retry=0;
    bool connected=false, fire_armed=false;
};
extern "C" DoomGamepad *doom_gamepad_create(void) {
    auto *pad=new(std::nothrow) DoomGamepad;
    if (!pad) return nullptr;
    pad->module=LoadLibraryExW(L"xinput1_4.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(!pad->module) pad->module=LoadLibraryExW(L"xinput9_1_0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if(pad->module) pad->get=reinterpret_cast<DoomGamepad::GetState>(GetProcAddress(pad->module,"XInputGetState"));
    return pad;
}
extern "C" void doom_gamepad_reset(DoomGamepad *pad) {
    if(!pad) return;
    pad->previous=XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT;
    pad->fire_armed=false;
}
extern "C" DoomGamepadState doom_gamepad_sample(DoomGamepad *pad,int enabled,int left_trigger,int reverse) {
    DoomGamepadState result{};
    XINPUT_STATE state{};
    if(!pad || !pad->get) return result;
    if(pad->connected && pad->get(pad->index,&state)!=ERROR_SUCCESS) {
        pad->connected=false; pad->retry=0; doom_gamepad_reset(pad);
    }
    if(!pad->connected) {
        const ULONGLONG now=GetTickCount64();
        if(now<pad->retry) return result;
        pad->retry=now+1000;
        for(DWORD i=0;i<XUSER_MAX_COUNT;i++) {
            if(pad->get(i,&state)==ERROR_SUCCESS) {
                pad->connected=true; pad->index=i; doom_gamepad_reset(pad); break;
            }
        }
        if(!pad->connected) return result;
    }
    const bool pressed=(left_trigger?state.Gamepad.bLeftTrigger:state.Gamepad.bRightTrigger)>XINPUT_GAMEPAD_TRIGGER_THRESHOLD;
    if(!pressed) pad->fire_armed=true;
    else if(!enabled) pad->fire_armed=false;
    const WORD buttons=state.Gamepad.wButtons;
    const WORD edges=static_cast<WORD>(buttons & ~pad->previous);
    pad->previous=buttons;
    if(enabled) {
        result.fire=pressed && pad->fire_armed;
        const bool left=(edges & XINPUT_GAMEPAD_DPAD_LEFT)!=0;
        const bool right=(edges & XINPUT_GAMEPAD_DPAD_RIGHT)!=0;
        if(left!=right) result.cycle=(left?-1:1)*(reverse?-1:1);
    }
    return result;
}
extern "C" void doom_gamepad_destroy(DoomGamepad *pad) {
    if(!pad) return;
    if(pad->module) FreeLibrary(pad->module);
    delete pad;
}
