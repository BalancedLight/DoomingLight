#include "doom_player_voice.h"
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <intrin.h>
#include <new>

#if defined(DOOM_MODE_RUNTIME) || (defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG))
namespace {
constexpr uintptr_t GameGlobal = 0x1D07F88, GameVtable = 0x157D5F8;
constexpr uintptr_t PlayerVtable = 0x17DD738, GetLocalPlayer = 0x3C2D90;
constexpr uintptr_t PlayerSound = 0xBE6660;
constexpr uintptr_t JumpSoundImport = 0x13CCC08, JumpSoundCall = 0xBE5A5F, JumpSoundReturn = 0xBE5A65;
constexpr uintptr_t Tables[] = {0x17ED148, 0x17E1188, 0x17E3D38};
constexpr unsigned SoundSlot = 364;
using PlaySound = unsigned (__fastcall *)(void *, unsigned, unsigned, unsigned char,
                                            float, float, float, int, float);
using GetPlayer = void *(__fastcall *)(void *);
using PlaySound2D = int (__fastcall *)(void *, const void *, unsigned char, float, float, float, unsigned, float, unsigned);
std::atomic<DoomPlayerVoice *> current{nullptr};
PlaySound original = nullptr;
PlaySound2D original2d = nullptr;
template<class T> T field(void *p, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(p) + offset);
}
}
struct DoomPlayerVoice {
    unsigned char *game = nullptr;
    std::atomic<bool> active{false};
    std::atomic<unsigned> pending_jumps{0};
    std::atomic<uint64_t> last_jump_ms{0};
    SRWLOCK observations_lock = SRWLOCK_INIT;
    DoomPlayerVoiceObservation observations[64]{};
    unsigned observations_head = 0, observations_count = 0;
    bool trace = false;
    bool installed[3]{};
    bool installed2d = false;
    bool published = false;
};
namespace {
struct NativeString { const char *data; unsigned size, capacity; };
bool copy_cue(const void *name,char out[128]) {
    out[0]=0;
    if(!name) return false;
    __try {
        const auto *s=static_cast<const NativeString *>(name);
        if(!s->data || !s->size || s->size>=128 || s->capacity<s->size) return false;
        for(unsigned i=0;i<s->size;i++) {
            unsigned char c=static_cast<unsigned char>(s->data[i]);
            if(c<32 || c>126) {out[0]=0;return false;}
            out[i]=static_cast<char>(c>='A'&&c<='Z'?c+('a'-'A'):c);
        }
        out[s->size]=0; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {out[0]=0;return false;}
}
bool exertion_cue(const char *name) {
    static const char *const prefixes[]={"playerm1_speech_edge_grab", "playerm1_speech_edge_grab_heavy",
        "playerm1_speech_before_landing_damper", "playerm1_speech_reaction_landing_damper",
        "playerm1_speech_reaction_landing_heavy_injuries"};
    const char *base=name;
    for(const char *p=name;*p;p++) if(*p=='/'||*p=='\\') base=p+1;
    for(const char *prefix:prefixes) {
        size_t n=std::strlen(prefix);
        if(std::strncmp(base,prefix,n) || base[n]!='_') continue;
        const char *suffix=base+n+1;
        if(!suffix[0] || !suffix[1]) continue;
        if(!((suffix[0]>='0'&&suffix[0]<='9'&&suffix[1]>='0'&&suffix[1]<='9') ||
             (suffix[0]=='x'&&suffix[1]=='x'))) continue;
        if(!suffix[2] || !std::strcmp(suffix+2,".wav")) return true;
    }
    return false;
}
void observe(DoomPlayerVoice *filter, uint32_t kind, unsigned category, const void *caller, const char *cue=nullptr) {
    if (!filter->trace || !TryAcquireSRWLockExclusive(&filter->observations_lock)) return;
    if (filter->observations_count < 64) {
        uintptr_t base=reinterpret_cast<uintptr_t>(filter->game);
        uintptr_t address=reinterpret_cast<uintptr_t>(caller);
        unsigned index=(filter->observations_head+filter->observations_count)%64;
        filter->observations[index]={kind,category,
            address>=base && address-base<0x2082000u ? static_cast<uint32_t>(address-base) : UINT32_MAX};
        if(cue) {
            size_t n=std::strlen(cue);
            if(n>=sizeof(filter->observations[index].cue)) n=sizeof(filter->observations[index].cue)-1;
            std::memcpy(filter->observations[index].cue,cue,n);
            filter->observations[index].cue[n]=0;
        }
        ++filter->observations_count;
    }
    ReleaseSRWLockExclusive(&filter->observations_lock);
}
void queue_jump(DoomPlayerVoice *filter) {
    const uint64_t now=GetTickCount64();
    uint64_t last=filter->last_jump_ms.load(std::memory_order_relaxed);
    do {
        if(last && now>=last && now-last<150) return;
    } while(!filter->last_jump_ms.compare_exchange_weak(last,now,
                 std::memory_order_acq_rel,std::memory_order_relaxed));
    unsigned count=filter->pending_jumps.load(std::memory_order_relaxed);
    while(count<32 && !filter->pending_jumps.compare_exchange_weak(
              count,count+1,std::memory_order_release,std::memory_order_relaxed)) {}
}
bool is_local_view(DoomPlayerVoice *filter, void *view) {
    if (!filter || !view) return false;
    __try {
        void *game = *reinterpret_cast<void **>(filter->game + GameGlobal);
        if (!game || *reinterpret_cast<void **>(game) != filter->game + GameVtable) return false;
        void *player = reinterpret_cast<GetPlayer>(filter->game + GetLocalPlayer)(game);
        if (!player || *reinterpret_cast<void **>(player) != filter->game + PlayerVtable) return false;
        const bool actor_sound = view == static_cast<unsigned char *>(player) + 40 &&
            field<void *>(player, 40) == filter->game + 0x17DE468;
        return (actor_sound || view == player || field<void *>(view, 0x50) == player) &&
               player == reinterpret_cast<GetPlayer>(filter->game + GetLocalPlayer)(game) &&
               (!actor_sound || field<void *>(player, 40) == filter->game + 0x17DE468);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned __fastcall play_hook(void *view, unsigned id, unsigned flags, unsigned char mode,
                               float p5, float p6, float p7, int p8, float p9) {
    void *caller = _ReturnAddress();
    DoomPlayerVoice *filter = current.load(std::memory_order_acquire);
    const bool target = (id >= 3 && id <= 6) || id == 9 || id == 16 || id == 32 || id == 33;
    if (filter && filter->active.load(std::memory_order_acquire) && (target || filter->trace) && is_local_view(filter, view)) {
        observe(filter,1,id,caller);
        if (target) { if(id == 16) queue_jump(filter); return 0; }
    }
    return original(view, id, flags, mode, p5, p6, p7, p8, p9);
}
int __fastcall sound2d_hook(void *view, const void *name, unsigned char enabled,
                            float volume, float pitch, float distance, unsigned flags,
                            float fade, unsigned category) {
    void *caller = _ReturnAddress();
    DoomPlayerVoice *filter = current.load(std::memory_order_acquire);
    if (filter && filter->active.load(std::memory_order_acquire)) {
        char cue[128]{};
        bool has_cue=copy_cue(name,cue);
        bool action=has_cue && exertion_cue(cue);
        if ((filter->trace || action || caller == filter->game + JumpSoundReturn) && is_local_view(filter, view)) {
            observe(filter,2,category,caller,has_cue?cue:nullptr);
            if (action || caller == filter->game + JumpSoundReturn) { queue_jump(filter); return 0; }
        }
    }
    return original2d(view, name, enabled, volume, pitch, distance, flags, fade, category);
}
bool replace_slot(void **slot, void *expected, void *replacement, bool *changed) {
    DWORD protection = 0;
    *changed = false;
    if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection)) return false;
    void *seen = InterlockedCompareExchangePointer(slot, replacement, expected);
    *changed = seen == expected;
    DWORD ignored = 0;
    return VirtualProtect(slot, sizeof(*slot), protection, &ignored) != 0 && *changed;
}
}
extern "C" DoomPlayerVoice *doom_player_voice_create(void *module) {
    if (!module || module != GetModuleHandleW(L"gamedll_x64_rwdi.dll")) return nullptr;
    auto *filter = new (std::nothrow) DoomPlayerVoice;
    if (filter) {
        filter->game = static_cast<unsigned char *>(module);
#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
        wchar_t value[4]{};
        filter->trace = GetEnvironmentVariableW(L"DOOMING_LIGHT_VOICE_TRACE",value,4)==1 && value[0]==L'1';
#endif
    }
    return filter;
}
extern "C" int doom_player_voice_install(DoomPlayerVoice *filter) {
    if (!filter || !filter->game) return 0;
    static const unsigned char prefix[] = {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10};
    static const unsigned char player_prefix[] = {0x48,0x8b,0x81,0x08,0x05,0,0,0x48,0x85,0xc0,0x74};
    static const unsigned char jump_call[] = {0xff,0x15,0xa3,0x71,0x7e,0x00};
    static const unsigned char sound2d_prefix[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48};
    HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!engine || filter->installed2d) return 0;
    void *sound2d = reinterpret_cast<void *>(GetProcAddress(engine,
        "?PlaySound2D@IGSObject@@QEAAHAEBV?$string_base@D@ttl@@_NMMMIMI@Z"));
    __try {
        if (std::memcmp(filter->game + PlayerSound, prefix, sizeof prefix) ||
            std::memcmp(filter->game + GetLocalPlayer, player_prefix, sizeof player_prefix) ||
            std::memcmp(filter->game + JumpSoundCall, jump_call, sizeof jump_call) ||
            sound2d != reinterpret_cast<unsigned char *>(engine) + 0x27a410 ||
            std::memcmp(sound2d, sound2d_prefix, sizeof sound2d_prefix) ||
            *reinterpret_cast<void **>(filter->game + JumpSoundImport) != sound2d) return 0;
        for (unsigned i = 0; i < 3; ++i) {
            if (filter->installed[i] || reinterpret_cast<void **>(filter->game + Tables[i])[SoundSlot] != filter->game + PlayerSound)
                return 0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
    DoomPlayerVoice *empty = nullptr;
    if (!current.compare_exchange_strong(empty, filter, std::memory_order_acq_rel)) return 0;
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&play_hook), &pinned)) {
        current.store(nullptr, std::memory_order_release); return 0;
    }
    filter->published = true;
    original = reinterpret_cast<PlaySound>(filter->game + PlayerSound);
    original2d = reinterpret_cast<PlaySound2D>(sound2d);
    for (unsigned i = 0; i < 3; ++i) {
        bool changed = false;
        bool ok = replace_slot(reinterpret_cast<void **>(filter->game + Tables[i]) + SoundSlot,
                               filter->game + PlayerSound, reinterpret_cast<void *>(&play_hook), &changed);
        filter->installed[i] = changed;
        if (!ok) { (void)doom_player_voice_uninstall(filter); return 0; }
    }
    bool changed = false;
    bool ok = replace_slot(reinterpret_cast<void **>(filter->game + JumpSoundImport), sound2d,
                           reinterpret_cast<void *>(&sound2d_hook), &changed);
    filter->installed2d = changed;
    if (!ok) { (void)doom_player_voice_uninstall(filter); return 0; }
    return 1;
}
extern "C" void doom_player_voice_set_active(DoomPlayerVoice *filter, int active) {
    if (filter) {
        if (active && !filter->active.load(std::memory_order_acquire)) {
            filter->pending_jumps.store(0, std::memory_order_release);
            filter->last_jump_ms.store(0, std::memory_order_release);
        }
        filter->active.store(active != 0, std::memory_order_release);
        if (!active) filter->pending_jumps.store(0, std::memory_order_release);
    }
}
extern "C" unsigned doom_player_voice_drain_jumps(DoomPlayerVoice *filter) {
    return filter ? filter->pending_jumps.exchange(0, std::memory_order_acq_rel) : 0;
}
extern "C" unsigned doom_player_voice_drain_observations(DoomPlayerVoice *filter, DoomPlayerVoiceObservation *out, unsigned capacity) {
    if (!filter || !out || !capacity || !TryAcquireSRWLockExclusive(&filter->observations_lock)) return 0;
    unsigned count=0;
    while (count<capacity && filter->observations_count) {
        out[count++]=filter->observations[filter->observations_head];
        filter->observations_head=(filter->observations_head+1)%64;
        --filter->observations_count;
    }
    ReleaseSRWLockExclusive(&filter->observations_lock);
    return count;
}
extern "C" int doom_player_voice_uninstall(DoomPlayerVoice *filter) {
    if (!filter) return 0;
    filter->active.store(false, std::memory_order_release);
    bool ok = true;
    if (filter->installed2d) {
        bool changed = false;
        bool restored = replace_slot(reinterpret_cast<void **>(filter->game + JumpSoundImport),
                                     reinterpret_cast<void *>(&sound2d_hook), reinterpret_cast<void *>(original2d), &changed);
        if (changed) filter->installed2d = false;
        if (!restored) ok = false;
    }
    for (unsigned i = 0; i < 3; ++i) if (filter->installed[i]) {
        bool changed = false;
        bool restored = replace_slot(reinterpret_cast<void **>(filter->game + Tables[i]) + SoundSlot,
                                     reinterpret_cast<void *>(&play_hook), filter->game + PlayerSound, &changed);
        if (changed) filter->installed[i] = false;
        if (!restored) ok = false;
    }
    if (ok) {
        DoomPlayerVoice *expected = filter;
        (void)current.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    }
    return ok ? 1 : 0;
}
extern "C" void doom_player_voice_destroy(DoomPlayerVoice *filter) {
    if (!filter) return;
    (void)doom_player_voice_uninstall(filter);
    if (!filter->published) delete filter;
}
#else
struct DoomPlayerVoice {};
extern "C" DoomPlayerVoice *doom_player_voice_create(void *) { return nullptr; }
extern "C" int doom_player_voice_install(DoomPlayerVoice *) { return 0; }
extern "C" void doom_player_voice_set_active(DoomPlayerVoice *, int) {}
extern "C" unsigned doom_player_voice_drain_jumps(DoomPlayerVoice *) { return 0; }
extern "C" unsigned doom_player_voice_drain_observations(DoomPlayerVoice *,DoomPlayerVoiceObservation *,unsigned) { return 0; }
extern "C" int doom_player_voice_uninstall(DoomPlayerVoice *) { return 1; }
extern "C" void doom_player_voice_destroy(DoomPlayerVoice *) {}
#endif
