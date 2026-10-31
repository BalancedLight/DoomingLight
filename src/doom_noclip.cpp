#include "doom_noclip.h"
#include "doom_core.h"

#if defined(DOOM_MODE_RUNTIME)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <atomic>
#include <cstring>
#include <mutex>

namespace {
struct EngineString { const char *data; unsigned size, capacity; };
struct Snapshot {
    void *client, *session, *input, *camera, *level_di;
    bool player, flying;
};
using InputKey = void (__fastcall *)(void *, int, unsigned char);
using GetLevel = void *(__fastcall *)(void *);
using GetSessionLevel = void *(__fastcall *)(void *);
using Toggle = void (__fastcall *)(void *, const EngineString *);

std::atomic<bool> enabled{false}, flying{false}, returned{false};
std::atomic<bool> cheats_enabled{true};
std::atomic<int> last_transition{0};
std::atomic<void *> expected_level{nullptr}, expected_session{nullptr}, expected_client{nullptr};
std::atomic<uint32_t> expected_generation{0};
unsigned char *game = nullptr;
GetLevel get_level = nullptr;
InputKey original = nullptr;
InputKey original_free_camera = nullptr;
void **patched_slot = nullptr;
void **patched_free_slot = nullptr;
std::mutex raw_mutex;
bool raw_held[256]{}, raw_swallowed[256]{};
void *raw_camera = nullptr;
char raw_prefix[16]{};
unsigned raw_prefix_length = 0;
ULONGLONG raw_prefix_time = 0;

template<class T> T field(const void *base, size_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(base)+offset);
}

bool current(Snapshot *out, int *reason=nullptr) {
    if (reason) *reason=1;
    if (!out || !enabled.load(std::memory_order_acquire) || !game || !get_level ||
        !expected_generation.load(std::memory_order_acquire)) return false;
    *out={};
    __try {
        if (reason) *reason=2;
        void *g=field<void *>(game,0x1d07f88);
        if (!g || field<void *>(g,0)!=game+0x157d5f8) return false;
        if (reason) *reason=3;
        void *session=field<void *>(g,0x508);
        if (!session || session!=expected_session.load(std::memory_order_acquire)) return false;
        out->session=session;
        if (reason) *reason=4;
        auto **table=*reinterpret_cast<void ***>(session);
        if ((table!=reinterpret_cast<void **>(game+0x1591fc8) &&
             table!=reinterpret_cast<void **>(game+0x158e488) &&
             table!=reinterpret_cast<void **>(game+0x158f838)) || table[244]!=game+0x6ad140) return false;
        if (reason) *reason=5;
        void *level_di=reinterpret_cast<GetSessionLevel>(table[244])(session);
        if (!level_di || field<void *>(level_di,0)!=game+0x1581c98 ||
            field<void *>(level_di,16)!=game+0x15822e8 ||
            get_level(static_cast<unsigned char *>(level_di)+16)!=expected_level.load(std::memory_order_acquire))
            return false;
        out->level_di=level_di;
        if (reason) *reason=6;
        void *client=field<void *>(session,0xb8);
        if (!client || client!=expected_client.load(std::memory_order_acquire) ||
            field<void *>(client,0)!=game+0x1583b88 || field<void *>(client,64)!=session) return false;
        out->client=client;
        if (reason) *reason=7;
        void *input=field<void *>(g,200), *camera=field<void *>(session,192);
        out->input=input; out->camera=camera;
        if (!input || !camera || !field<void *>(camera,64)) return false;
        if (reason) *reason=8;
        const bool owner=field<void *>(client,80)!=nullptr;
        const bool actor=field<void *>(client,88)!=nullptr;
        if (owner!=actor) return false;
        out->player=owner && actor;
        out->flying=!owner && !actor && field<unsigned char>(client,4528)!=0 && field<int>(camera,88)==0;
        if (reason) *reason=9;
        if (g!=field<void *>(game,0x1d07f88) || session!=field<void *>(g,0x508) ||
            client!=field<void *>(session,0xb8)) return false;
        if (reason) *reason=0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { if (reason) *reason=10; return false; }
}

bool input_ready(const Snapshot &snapshot) {
    DWORD pid=0;
    GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    if (pid!=GetCurrentProcessId()) return false;
    const int blocked=field<int>(snapshot.input,308);
    if (blocked==0) return true;
    if (!snapshot.flying || blocked!=1 || !flying.load(std::memory_order_acquire)) return false;
    auto *camera=static_cast<unsigned char *>(field<void *>(snapshot.camera,80));
    if (!camera || field<void *>(camera,16)!=game+0x19248e8 ||
        field<unsigned char>(camera,716)!=1) return false;
    /* FreeCamera owns one Player-target block. Any additional block belongs
       to another system, such as a menu, and keeps gameplay input suspended. */
    return field<void *>(snapshot.camera,80)==camera &&
        field<unsigned char>(camera,716)==1 && field<int>(snapshot.input,308)==1;
}

bool own_free_camera(void *object) {
    Snapshot s{};
    __try {
        return object && flying.load(std::memory_order_acquire) && current(&s) && s.flying &&
            input_ready(s) && field<void *>(object,0)==game+0x19248e8 &&
            field<void *>(s.camera,80)==static_cast<unsigned char *>(object)-16;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool has_cheat_extension(char *candidate, unsigned length) {
    if (length+1u>=sizeof(raw_prefix)) return false;
    for (char ch='A'; ch<='Z'; ++ch) {
        candidate[length]=ch;
        if (doom_cheat_is_prefix(candidate,length+1u)) return true;
    }
    for (char ch='0'; ch<='9'; ++ch) {
        candidate[length]=ch;
        if (doom_cheat_is_prefix(candidate,length+1u)) return true;
    }
    return false;
}

bool swallow_free_camera_key(void *object, int key, unsigned char pressed) {
    if (key<0 || key>255) return false;
    std::lock_guard<std::mutex> lock(raw_mutex);
    const unsigned index=static_cast<unsigned>(key);
    if (!pressed) {
        raw_held[index]=false;
        if (raw_swallowed[index] && raw_camera==object) {
            raw_swallowed[index]=false;
            return true;
        }
        return false;
    }
    if (raw_held[index]) return raw_swallowed[index] && raw_camera==object;
    raw_held[index]=true;
    if (!cheats_enabled.load(std::memory_order_acquire) || !own_free_camera(object)) {
        raw_prefix_length=0; raw_prefix_time=0; return false;
    }
    const ULONGLONG now=GetTickCount64();
    if (raw_prefix_time && now-raw_prefix_time>2000u) raw_prefix_length=0;
    const UINT vk=MapVirtualKeyW(index,MAPVK_VSC_TO_VK);
    if (!((vk>='A' && vk<='Z') || (vk>='0' && vk<='9'))) {
        if (vk==VK_ESCAPE) raw_prefix_length=0;
        return false;
    }
    char candidate[sizeof(raw_prefix)]{};
    unsigned length=raw_prefix_length;
    if (length) std::memcpy(candidate,raw_prefix,length);
    candidate[length++]=static_cast<char>(vk);
    if (!doom_cheat_is_prefix(candidate,length)) {
        length=1; candidate[0]=static_cast<char>(vk);
        if (!doom_cheat_is_prefix(candidate,length)) {
            raw_prefix_length=0; raw_prefix_time=0; return false;
        }
    }
    raw_swallowed[index]=true; raw_camera=object;
    if (has_cheat_extension(candidate,length)) {
        std::memcpy(raw_prefix,candidate,length);
        raw_prefix_length=length; raw_prefix_time=now;
    } else {
        raw_prefix_length=0; raw_prefix_time=0;
    }
    return true;
}

void __fastcall free_camera_input_hook(void *object, int key, unsigned char pressed) {
    if (!swallow_free_camera_key(object,key,pressed)) original_free_camera(object,key,pressed);
}

void record_transition(const Snapshot &before, const Snapshot &after) {
    if (before.client!=after.client || before.session!=after.session) return;
    if (before.player && after.flying) {
        returned.store(false,std::memory_order_release);
        flying.store(true,std::memory_order_release);
        last_transition.store(1,std::memory_order_release);
    } else if (before.flying && after.player && flying.exchange(false,std::memory_order_acq_rel)) {
        returned.store(true,std::memory_order_release);
        last_transition.store(2,std::memory_order_release);
    } else {
        last_transition.store(3,std::memory_order_release);
    }
}

void __fastcall input_hook(void *client, int key, unsigned char pressed) {
    Snapshot before{}, after{};
    bool observe=false;
    __try { observe=key==21 && pressed && current(&before) && before.client==client && input_ready(before); }
    __except(EXCEPTION_EXECUTE_HANDLER) { observe=false; }
    original(client,key,pressed);
    if (observe) {
        int reason=0;
        if (current(&after,&reason)) record_transition(before,after);
        else last_transition.store(300+reason,std::memory_order_release);
    }
}
}

extern "C" int doom_noclip_start(void *game_module, void *engine_module, void *level,
                                   uint32_t generation, int supported_profile) {
    auto *base=static_cast<unsigned char *>(game_module);
    auto engine=static_cast<HMODULE>(engine_module);
    static const unsigned char input_prefix[]={0x40,0x53,0x55,0x41,0x54,0x48,0x83,0xec,0x50,0x48,0x8b,0xe9};
    static const unsigned char toggle_prefix[]={0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x48,0x83,0x79,0x50,0x00,0x48};
    static const unsigned char free_input_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57};
    if (!supported_profile || !base || !engine || !level || !generation ||
        GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=game_module ||
        GetModuleHandleW(L"engine_x64_rwdi.dll")!=engine_module ||
        std::memcmp(base+0x40c2f0,input_prefix,sizeof(input_prefix)) ||
        std::memcmp(base+0x40c1b0,toggle_prefix,sizeof(toggle_prefix)) ||
        std::memcmp(base+0x11441f0,free_input_prefix,sizeof(free_input_prefix))) return 0;
    auto get=reinterpret_cast<GetLevel>(GetProcAddress(engine,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    if (!get) return 0;
    __try {
        void *g=field<void *>(base,0x1d07f88);
        void *session=g ? field<void *>(g,0x508) : nullptr;
        void *client=session ? field<void *>(session,0xb8) : nullptr;
        auto **slot=reinterpret_cast<void **>(base+0x1584008);
        auto **free_slot=reinterpret_cast<void **>(base+0x1924d68);
        if (!g || !session || !client || field<void *>(client,0)!=base+0x1583b88 ||
            field<void *>(client,64)!=session || *slot!=base+0x40c2f0 ||
            *free_slot!=base+0x11441f0 || patched_slot || patched_free_slot) return 0;
        HMODULE self=nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                                reinterpret_cast<LPCWSTR>(&input_hook),&self)) return 0;
        game=base; get_level=get; original=reinterpret_cast<InputKey>(base+0x40c2f0);
        original_free_camera=reinterpret_cast<InputKey>(base+0x11441f0);
        expected_level.store(level,std::memory_order_release);
        expected_session.store(session,std::memory_order_release);
        expected_client.store(client,std::memory_order_release);
        expected_generation.store(generation,std::memory_order_release);
        flying.store(false,std::memory_order_release); returned.store(false,std::memory_order_release);
        last_transition.store(0,std::memory_order_release);
        enabled.store(true,std::memory_order_release);
        Snapshot initial{};
        if (!current(&initial) || !initial.player) { enabled.store(false,std::memory_order_release); return 0; }
        DWORD old=0;
        if (!VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&old)) { enabled.store(false,std::memory_order_release); return 0; }
        void *seen=InterlockedCompareExchangePointer(slot,reinterpret_cast<void *>(&input_hook),base+0x40c2f0);
        DWORD ignored=0;
        bool restored=VirtualProtect(slot,sizeof(void *),old,&ignored)!=0;
        if (seen!=base+0x40c2f0) { enabled.store(false,std::memory_order_release); return 0; }
        patched_slot=slot;
        if (!restored) { (void)doom_noclip_stop(); return 0; }
        if (!VirtualProtect(free_slot,sizeof(void *),PAGE_READWRITE,&old)) { (void)doom_noclip_stop(); return 0; }
        seen=InterlockedCompareExchangePointer(free_slot,reinterpret_cast<void *>(&free_camera_input_hook),base+0x11441f0);
        restored=VirtualProtect(free_slot,sizeof(void *),old,&ignored)!=0;
        if (seen==base+0x11441f0) patched_free_slot=free_slot;
        if (seen!=base+0x11441f0 || !restored) { (void)doom_noclip_stop(); return 0; }
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { enabled.store(false,std::memory_order_release); return 0; }
}

extern "C" int doom_noclip_toggle(void) {
    Snapshot before{},after{};
    __try {
        int reason=0;
        if (!current(&before,&reason)) { last_transition.store(100+reason,std::memory_order_release); return 0; }
        if (!input_ready(before) || (field<unsigned char>(before.level_di,3428)&1u) ||
            (!before.player && !(before.flying && flying.load(std::memory_order_acquire)))) {
            last_transition.store(111,std::memory_order_release); return 0;
        }
        EngineString empty{};
        reinterpret_cast<Toggle>(game+0x40c1b0)(before.client,&empty);
        if (!current(&after,&reason)) { last_transition.store(200+reason,std::memory_order_release); return 0; }
        record_transition(before,after);
        return (before.player && after.flying) || (before.flying && after.player);
    } __except(EXCEPTION_EXECUTE_HANDLER) { last_transition.store(210,std::memory_order_release); return 0; }
}

extern "C" int doom_noclip_active(void) {
    Snapshot s{};
    return flying.load(std::memory_order_acquire) && current(&s) && s.flying;
}

extern "C" int doom_noclip_input_ready(void) {
    Snapshot s{};
    __try { return flying.load(std::memory_order_acquire) && current(&s) && s.flying && input_ready(s); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

extern "C" void doom_noclip_set_cheats_enabled(int value) {
    cheats_enabled.store(value!=0,std::memory_order_release);
}

extern "C" void doom_noclip_diagnostic(DoomNoclipDiagnostic *out) {
    if (!out) return;
    *out={};
    out->camera_mode=out->saved=out->owner=out->actor=out->input_mode=-1;
    out->enabled=enabled.load(std::memory_order_acquire) ? 1 : 0;
    out->tracked=flying.load(std::memory_order_acquire) ? 1 : 0;
    out->last_transition=last_transition.load(std::memory_order_acquire);
    Snapshot s{};
    (void)current(&s,&out->validation);
    __try {
        if (s.camera) out->camera_mode=field<int>(s.camera,88);
        if (s.client) {
            out->saved=field<unsigned char>(s.client,4528)!=0;
            out->owner=field<void *>(s.client,80)!=nullptr;
            out->actor=field<void *>(s.client,88)!=nullptr;
        }
        if (s.input) out->input_mode=field<int>(s.input,308);
    } __except(EXCEPTION_EXECUTE_HANDLER) { out->validation=10; }
}

extern "C" int doom_noclip_take_returned(void) {
    return returned.exchange(false,std::memory_order_acq_rel) ? 1 : 0;
}

extern "C" int doom_noclip_stop(void) {
    enabled.store(false,std::memory_order_release);
    flying.store(false,std::memory_order_release);
    returned.store(false,std::memory_order_release);
    void ***slots[]={&patched_free_slot,&patched_slot};
    void *hooks[]={reinterpret_cast<void *>(&free_camera_input_hook),reinterpret_cast<void *>(&input_hook)};
    void *originals[]={reinterpret_cast<void *>(original_free_camera),reinterpret_cast<void *>(original)};
    bool success=true;
    for (unsigned i=0; i<2; ++i) {
        void **slot=*slots[i];
        if (!slot) continue;
        DWORD old=0;
        if (!VirtualProtect(slot,sizeof(void *),PAGE_READWRITE,&old)) { success=false; continue; }
        void *seen=InterlockedCompareExchangePointer(slot,originals[i],hooks[i]);
        DWORD ignored=0;
        const bool restored=VirtualProtect(slot,sizeof(void *),old,&ignored)!=0;
        if ((seen!=hooks[i] && seen!=originals[i]) || !restored) success=false;
        else *slots[i]=nullptr;
    }
    return success ? 1 : 0;
}
#else
extern "C" int doom_noclip_start(void *, void *, void *, uint32_t, int) { return 0; }
extern "C" int doom_noclip_toggle(void) { return 0; }
extern "C" int doom_noclip_active(void) { return 0; }
extern "C" int doom_noclip_input_ready(void) { return 0; }
extern "C" void doom_noclip_set_cheats_enabled(int) {}
extern "C" void doom_noclip_diagnostic(DoomNoclipDiagnostic *out) {
    if (out) { *out={}; out->validation=1; out->camera_mode=out->saved=out->owner=out->actor=out->input_mode=-1; }
}
extern "C" int doom_noclip_take_returned(void) { return 0; }
extern "C" int doom_noclip_stop(void) { return 1; }
#endif
