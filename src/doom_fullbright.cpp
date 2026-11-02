#include "doom_fullbright.h"
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(DOOM_MODE_RUNTIME)
namespace {
struct TtlString { char *data; uint32_t size, capacity; };
using GetVarSet = void *(__fastcall *)(void *);
using GetScript = TtlString *(__fastcall *)(void *, TtlString *);
using SetScript = void (__fastcall *)(void *, const TtlString *);
using GetFloat = float (__fastcall *)(void *, const TtlString *, void *);
using SetFloat = unsigned char (__fastcall *)(void *, const TtlString *, float, void *);
using FindEffect = void *(__fastcall *)(void *, const char *);
using NativeFree = void (__cdecl *)(void *);

constexpr char Variable[] = "f_ed_pp_gbuffer_diffuse_on";
constexpr char Simple[] = "simple";
TtlString name_view(const char *text) {
    const auto length = static_cast<uint32_t>(std::strlen(text));
    return {const_cast<char *>(text), length, length};
}
}

struct DoomFullbright {
    unsigned char *game, *engine;
    void *level, *tools, *manager, *varset;
    uint32_t generation;
    GetVarSet get_varset;
    GetScript get_script;
    SetScript set_script;
    GetFloat get_float;
    SetFloat set_float;
    FindEffect find_effect;
    NativeFree native_free;
    char previous_script[512];
    float previous_value;
    bool script_owned, script_maybe_empty, variable_owned, blocked;
};

namespace {
bool base_valid(DoomFullbright *host, void *level, uint32_t generation) {
    if (!host || !level || host->level != level || host->generation != generation) return false;
    __try {
        if (GetModuleHandleW(L"gamedll_x64_rwdi.dll") != reinterpret_cast<HMODULE>(host->game) ||
            GetModuleHandleW(L"engine_x64_rwdi.dll") != reinterpret_cast<HMODULE>(host->engine)) return false;
        auto **level_table = *reinterpret_cast<void ***>(level);
        if (level_table != reinterpret_cast<void **>(host->game + 0x1581c98) ||
            level_table[2] != host->game + 0x130966e ||
            level_table[3] != host->game + 0x130962c ||
            level_table[37] != host->game + 0x13096e0 ||
            level_table[38] != host->game + 0x13096e6) return false;
        void *tools = *reinterpret_cast<void **>(host->engine + 0xaccc70);
        void *manager = *reinterpret_cast<void **>(host->engine + 0xaccab0);
        if (!tools || tools != host->tools || !manager || manager != host->manager ||
            *reinterpret_cast<void **>(tools) != host->engine + 0x8d55c8 ||
            *reinterpret_cast<void **>(manager) != host->engine + 0x943728 ||
            !*reinterpret_cast<void **>(host->engine + 0xacd130)) return false;
        auto **table = *reinterpret_cast<void ***>(tools);
        if (table[91] != host->engine + 0x28c4a0 || table[96] != host->engine + 0x28cd70)
            return false;
        return *reinterpret_cast<void **>(host->engine + 0xaccc70) == tools &&
            *reinterpret_cast<void **>(host->engine + 0xaccab0) == manager;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool sync_varset(DoomFullbright *host, void *level, uint32_t generation, bool *replaced) {
    if (!replaced || !base_valid(host, level, generation)) return false;
    *replaced = false;
    __try {
        void *current = host->get_varset(level);
        if (!current || !base_valid(host, level, generation)) return false;
        if (current != host->varset) {
            /* The retired set may already be gone; never access it again. */
            host->variable_owned = false;
            host->varset = current;
            host->blocked = true;
            *replaced = true;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool valid(DoomFullbright *host, void *level, uint32_t generation) {
    if (!base_valid(host, level, generation)) return false;
    __try {
        return host->get_varset(level) == host->varset && host->varset &&
            base_valid(host, level, generation);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool get_script(DoomFullbright *host, char (&out)[512]) {
    TtlString value{};
    bool ok = false;
    __try {
        if (host->get_script(host->level, &value) == &value && value.size < sizeof(out) &&
            value.size <= value.capacity && (!value.size || value.data)) {
            if (value.size) std::memcpy(out, value.data, value.size);
            out[value.size] = 0;
            ok = true;
        }
    } __finally {
        if (value.data) host->native_free(value.data);
    }
    return ok;
}
bool script_exists(DoomFullbright *host, const char *name) {
    return !*name || host->find_effect(host->manager, name) != nullptr;
}
float get_value(DoomFullbright *host) {
    TtlString name = name_view(Variable);
    return host->get_float(host->tools, &name, host->varset);
}
bool set_value(DoomFullbright *host, float value) {
    TtlString name = name_view(Variable);
    return host->set_float(host->tools, &name, value, host->varset) != 0;
}
void set_script(DoomFullbright *host, const char *value) {
    TtlString name = name_view(value);
    host->set_script(host->level, &name);
}

/* Keep ownership when a native operation fails so the caller can retry. */
bool restore(DoomFullbright *host) {
    if (!valid(host, host->level, host->generation)) return false;
    __try {
        bool ok = true;
        if (host->script_owned) {
            char current[512]{};
            if (!get_script(host, current)) ok = false;
            else if (!std::strcmp(current, Simple) ||
                     (host->script_maybe_empty && !current[0] && host->previous_script[0])) {
                if (!script_exists(host, host->previous_script)) ok = false;
                else {
                    set_script(host, host->previous_script);
                    if (!get_script(host, current) || std::strcmp(current, host->previous_script)) ok = false;
                    else { host->script_owned = false; host->script_maybe_empty = false; }
                }
            } else {
                host->script_owned = false;
                host->script_maybe_empty = false;
            }
        }
        if (host->variable_owned) {
            const float current = get_value(host);
            if (!std::isfinite(current)) ok = false;
            else if (current == 1.0f && host->previous_value != 1.0f &&
                     (!set_value(host, host->previous_value) || get_value(host) != host->previous_value)) ok = false;
            else host->variable_owned = false;
        }
        return ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}

extern "C" DoomFullbright *doom_fullbright_create(void *game, void *engine, void *level, uint32_t generation) {
    if (!game || !engine || !level || !generation) return nullptr;
    auto *host = static_cast<DoomFullbright *>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DoomFullbright)));
    if (!host) return nullptr;
    host->game = static_cast<unsigned char *>(game);
    host->engine = static_cast<unsigned char *>(engine);
    host->level = level; host->generation = generation;
    bool ok = false;
    __try {
        HMODULE module = static_cast<HMODULE>(engine);
        HMODULE crt = GetModuleHandleW(L"MSVCR100.dll");
        if (GetModuleHandleW(L"gamedll_x64_rwdi.dll") != static_cast<HMODULE>(game) ||
            GetModuleHandleW(L"engine_x64_rwdi.dll") != module) __leave;
        static const unsigned char lookup_prefix[] = {0x48,0x89,0x5c,0x24,0x08,0x44,0x8b,0x99,0x38,0x18,0,0};
        if (std::memcmp(host->engine + 0x7bcf90, lookup_prefix, sizeof(lookup_prefix))) __leave;
        host->get_varset = reinterpret_cast<GetVarSet>(GetProcAddress(module, "?GetVarSet@ILevel@@UEBAPEAVCVarSet@@XZ"));
        host->get_script = reinterpret_cast<GetScript>(GetProcAddress(module, "?GetSceneRenderScript@ILevel@@UEBA?AV?$string_base@D@ttl@@XZ"));
        host->set_script = reinterpret_cast<SetScript>(GetProcAddress(module, "?SetSceneRenderScript@ILevel@@UEAAXAEBV?$string_base@D@ttl@@@Z"));
        host->native_free = crt ? reinterpret_cast<NativeFree>(GetProcAddress(crt, "free")) : nullptr;
        host->tools = *reinterpret_cast<void **>(host->engine + 0xaccc70);
        host->manager = *reinterpret_cast<void **>(host->engine + 0xaccab0);
        if (reinterpret_cast<void *>(host->get_varset) == host->engine + 0x2cf7b0 &&
            reinterpret_cast<void *>(host->get_script) == host->engine + 0x2d7810 &&
            reinterpret_cast<void *>(host->set_script) == host->engine + 0x2d77e0 &&
            host->native_free && host->tools && host->manager &&
            *reinterpret_cast<void **>(host->tools) == host->engine + 0x8d55c8 &&
            *reinterpret_cast<void **>(host->manager) == host->engine + 0x943728 &&
            *reinterpret_cast<void **>(level) == host->game + 0x1581c98 &&
            (*reinterpret_cast<void ***>(level))[2] == host->game + 0x130966e &&
            (*reinterpret_cast<void ***>(level))[3] == host->game + 0x130962c &&
            (*reinterpret_cast<void ***>(level))[37] == host->game + 0x13096e0 &&
            (*reinterpret_cast<void ***>(level))[38] == host->game + 0x13096e6) {
            auto **table = *reinterpret_cast<void ***>(host->tools);
            host->get_float = reinterpret_cast<GetFloat>(table[96]);
            host->set_float = reinterpret_cast<SetFloat>(table[91]);
            host->find_effect = reinterpret_cast<FindEffect>(host->engine + 0x7bcf90);
            host->varset = host->get_varset(level);
            ok = valid(host, level, generation);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) { HeapFree(GetProcessHeap(), 0, host); return nullptr; }
    return host;
}

extern "C" int doom_fullbright_update(DoomFullbright *host, void *level, uint32_t generation, int enabled) {
    bool replaced = false;
    if (!sync_varset(host, level, generation, &replaced)) return 0;
    if (!enabled) {
        const bool ok = restore(host);
        if (ok) host->blocked = false;
        return ok ? 1 : 0;
    }
    if (host->blocked) {
        if (host->script_owned || host->variable_owned) (void)restore(host);
        return 0;
    }
    __try {
        if (host->variable_owned || host->script_owned) {
            char current[512]{};
            if (!get_script(host, current)) return 0;
            const float value = get_value(host);
            if (host->variable_owned && host->script_owned && !std::strcmp(current, Simple) && value == 1.0f)
                return 1;
            host->blocked = true;
            (void)restore(host);
            return 0;
        }
        char before[512]{};
        if (!script_exists(host, Simple) || !get_script(host, before) || !script_exists(host, before)) return 0;
        const float previous = get_value(host);
        if (!std::isfinite(previous)) return 0;
        std::memcpy(host->previous_script, before, std::strlen(before) + 1);
        host->previous_value = previous;
        host->variable_owned = true;
        if (!set_value(host, 1.0f) || get_value(host) != 1.0f) {
            (void)restore(host); host->blocked = true; return 0;
        }
        host->script_owned = true;
        host->script_maybe_empty = true;
        set_script(host, Simple);
        char current[512]{};
        if (!get_script(host, current) || std::strcmp(current, Simple)) {
            (void)restore(host); host->blocked = true; return 0;
        }
        host->script_maybe_empty = false;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        host->blocked = true;
        (void)restore(host);
        return 0;
    }
}

extern "C" int doom_fullbright_destroy(DoomFullbright *host, void *level, uint32_t generation) {
    if (!host) return 1;
    bool replaced = false;
    if (!sync_varset(host, level, generation, &replaced) || !restore(host)) return 0;
    HeapFree(GetProcessHeap(), 0, host);
    return 1;
}
extern "C" void doom_fullbright_forget(DoomFullbright *host) {
    if (host) HeapFree(GetProcessHeap(), 0, host);
}
#else
extern "C" DoomFullbright *doom_fullbright_create(void *, void *, void *, uint32_t) { return nullptr; }
extern "C" int doom_fullbright_update(DoomFullbright *, void *, uint32_t, int) { return 0; }
extern "C" int doom_fullbright_destroy(DoomFullbright *, void *, uint32_t) { return 1; }
extern "C" void doom_fullbright_forget(DoomFullbright *) {}
#endif
