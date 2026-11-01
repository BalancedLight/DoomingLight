#include "doom_invuln_effect.h"
#include "doom_invuln_shader.h"
#include <windows.h>
#include <d3d11.h>
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstring>

namespace {
using FindMaterial = void *(__fastcall *)(void *, const char *, int);
using FindVar = void *(__fastcall *)(const char *, void *, int);
using GetVarValue = float *(__fastcall *)(void *, unsigned, int);
using Selector = uintptr_t (__fastcall *)(int);
using SetShader = void (STDMETHODCALLTYPE *)(ID3D11DeviceContext *, ID3D11PixelShader *, ID3D11ClassInstance *const *, UINT);

SRWLOCK shader_lock = SRWLOCK_INIT, selector_lock = SRWLOCK_INIT;
unsigned char *engine_base = nullptr;
ID3D11DeviceContext *context = nullptr;
ID3D11Device *context_device = nullptr;
ID3D11PixelShader *stock_shader = nullptr;
DoomInvulnShader *replacement = nullptr;
Selector selector_original = nullptr;
SetShader set_shader_original = nullptr;
void *shader_hook_target = nullptr;
std::atomic<bool> active{false};
std::atomic<const char *> stage{"not_started"};
std::atomic<unsigned> shader_calls{0}, stock_binds{0}, substitutions{0}, selector_calls{0};
bool selector_created = false, shader_created = false;
bool selector_enabled = false, shader_enabled = false;
float restore_slot = 0.0f;
ULONGLONG next_device_check = 0;

bool checked_engine(unsigned char *engine) {
    static const struct { unsigned rva; unsigned char bytes[12]; } checks[] = {
        {0x7be9e0,{0x48,0x89,0x5c,0x24,0x18,0x89,0x4c,0x24,0x08,0x55,0x56,0x57}},
        {0x7d5570,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89}},
        {0x7d1c30,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48}},
        {0x805ed0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48}}
    };
    if (!engine || GetModuleHandleW(L"engine_x64_rwdi.dll") != reinterpret_cast<HMODULE>(engine)) return false;
    __try {
        for (const auto &c : checks) if (std::memcmp(engine + c.rva, c.bytes, sizeof(c.bytes))) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

ID3D11PixelShader *find_stock_shader(unsigned char *engine, ID3D11Device **device_out) {
    if (!device_out) return nullptr;
    *device_out = nullptr;
    __try {
        stage.store("material_manager", std::memory_order_relaxed);
        void *manager = *reinterpret_cast<void **>(engine + 0xacd118);
        if (!manager || *reinterpret_cast<void **>(manager) != engine + 0x94ac48) return nullptr;
        auto **vt = *reinterpret_cast<void ***>(manager);
        if (vt[6] != engine + 0x805ed0) return nullptr;
        stage.store("material_not_found", std::memory_order_relaxed);
        for (int flags = 0; flags < 8; ++flags) {
            auto *handle = static_cast<unsigned char *>(reinterpret_cast<FindMaterial>(vt[6])(
                manager, "postprocess_effect_0.mat", flags));
            if (!handle) continue;
            stage.store("material_handle", std::memory_order_relaxed);
            auto *material = *reinterpret_cast<unsigned char **>(handle + 8);
            if (!material) continue;
            auto *record = *reinterpret_cast<unsigned char **>(material);
            auto *pack = *reinterpret_cast<unsigned char **>(material + 24);
            if (!record || !pack) continue;
            /* Pack resolution replaces the serialized name hash with an
             * owned filename pointer. Validate the live name, not disk bytes. */
            const char *name = *reinterpret_cast<const char **>(record);
            static const char expected_name[] = "postprocess_effect_0.mat";
            if (!name || std::memcmp(name, expected_name, sizeof(expected_name))) continue;
            stage.store("shader_table", std::memory_order_relaxed);
            auto *entries = *reinterpret_cast<unsigned char **>(pack + 336);
            const unsigned count = *reinterpret_cast<unsigned *>(pack + 344);
            if (!entries || !count || count > 100000u) continue;
            unsigned lo = 0, hi = count;
            while (lo < hi) {
                const unsigned mid = lo + (hi - lo) / 2;
                if (*reinterpret_cast<unsigned *>(entries + mid * 16u) < 0xa94963e1u) lo = mid + 1;
                else hi = mid;
            }
            if (lo >= count || *reinterpret_cast<unsigned *>(entries + lo * 16u) != 0xa94963e1u) continue;
            stage.store("shader_record", std::memory_order_relaxed);
            auto *high = *reinterpret_cast<unsigned char **>(entries + lo * 16u + 8);
            /* Template resolution installs the high-level key at +24. */
            if (!high || *reinterpret_cast<unsigned *>(high + 24) != 0xa94963e1u ||
                (high[3] & 7u)) continue;
            auto *wrapper = *reinterpret_cast<unsigned char **>(high + 16);
            auto *candidate = wrapper ? *reinterpret_cast<IUnknown **>(wrapper) : nullptr;
            if (!candidate) continue;
            stage.store("shader_device", std::memory_order_relaxed);
            /* The pack lookup is only accepted when its wrapper contains a
             * real D3D11 object; do not call an arbitrary renderer vtable. */
            auto **candidate_vtable = *reinterpret_cast<void ***>(candidate);
            HMODULE owner_module = nullptr;
            if (!candidate_vtable || !candidate_vtable[0] ||
                !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                    reinterpret_cast<LPCWSTR>(candidate_vtable[0]), &owner_module) ||
                owner_module != GetModuleHandleW(L"d3d11.dll")) continue;
            ID3D11PixelShader *pixel = nullptr;
            if (FAILED(candidate->QueryInterface(__uuidof(ID3D11PixelShader), reinterpret_cast<void **>(&pixel))) || !pixel)
                continue;
            ID3D11Device *owner = nullptr;
            pixel->GetDevice(&owner);
            if (owner) { *device_out = owner; return pixel; }
            pixel->Release();
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { stage.store("material_exception", std::memory_order_relaxed); return nullptr; }
    return nullptr;
}

void discard_device_objects(ID3D11PixelShader *shader, ID3D11Device *device, ID3D11DeviceContext *ctx) {
    if (ctx) ctx->Release();
    if (device) device->Release();
    if (shader) shader->Release();
}

bool slot_zero(float *out, const float *write) {
    if (!engine_base) return false;
    __try {
        auto *manager = *reinterpret_cast<unsigned char **>(engine_base + 0xacd130);
        if (!manager || !*reinterpret_cast<void **>(manager)) return false;
        auto *var = static_cast<unsigned char *>(reinterpret_cast<FindVar>(engine_base + 0x7d5570)(
            "f_game_postprocess_effect_0", manager + 320, 1));
        auto **vt = *reinterpret_cast<void ***>(manager);
        if (!var || !vt[9]) return false;
        void *set = reinterpret_cast<void *(__fastcall *)(void *)>(vt[9])(manager);
        if (!set) return false;
        const unsigned offset = *reinterpret_cast<unsigned *>(var + 80);
        float *value = reinterpret_cast<GetVarValue>(engine_base + 0x7d1c30)(set, offset, 4);
        if (!value || !std::isfinite(*value)) return false;
        if (out) *out = *value;
        if (write) {
            *value = *write;
            if (*reinterpret_cast<unsigned *>(static_cast<unsigned char *>(set) + 8) >= 0xfffffffeu) {
                void *notify = reinterpret_cast<void *(__fastcall *)()>(engine_base + 0x2f770)();
                reinterpret_cast<void (__fastcall *)(void *, void *)>(engine_base + 0x2f5f0)(notify, set);
            }
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void STDMETHODCALLTYPE shader_hook(ID3D11DeviceContext *ctx, ID3D11PixelShader *pixel,
                                    ID3D11ClassInstance *const *instances, UINT count) {
    AcquireSRWLockShared(&shader_lock);
    __try {
        if (active.load(std::memory_order_acquire)) {
            shader_calls.fetch_add(1, std::memory_order_relaxed);
            if (ctx == context && pixel == stock_shader) {
                stock_binds.fetch_add(1, std::memory_order_relaxed);
                if (replacement) {
                    substitutions.fetch_add(1, std::memory_order_relaxed);
                    pixel = static_cast<ID3D11PixelShader *>(doom_invuln_shader_pixel(replacement));
                }
            }
        }
        set_shader_original(ctx, pixel, instances, count);
    } __finally { ReleaseSRWLockShared(&shader_lock); }
}

uintptr_t __fastcall selector_hook(int value) {
    uintptr_t result = 0;
    AcquireSRWLockExclusive(&selector_lock);
    __try {
        selector_calls.fetch_add(1, std::memory_order_relaxed);
        result = selector_original(value);
        if (active.load(std::memory_order_acquire)) {
            const float enabled = 1.0f;
            restore_slot = value == 0 ? 1.0f : 0.0f;
            (void)slot_zero(nullptr, &enabled);
        }
    } __finally { ReleaseSRWLockExclusive(&selector_lock); }
    return result;
}
}

extern "C" int doom_invuln_effect_start(void *engine_module, void *renderer_module) {
    auto *engine = static_cast<unsigned char *>(engine_module);
    (void)renderer_module;
    if (selector_enabled && shader_enabled) return engine == engine_base;
    stage.store("module_guard", std::memory_order_relaxed);
    if (!checked_engine(engine)) return 0;
    __try {
        ID3D11Device *device = nullptr;
        auto *stock = find_stock_shader(engine, &device);
        if (!stock) return 0;
        stage.store("device_context", std::memory_order_relaxed);
        ID3D11DeviceContext *ctx = nullptr;
        device->GetImmediateContext(&ctx);
        if (!ctx) { discard_device_objects(stock, device, ctx); return 0; }
        stage.store("shader_compile_or_create", std::memory_order_relaxed);
        DoomInvulnShader *next = doom_invuln_shader_create(device);
        if (!next) { discard_device_objects(stock, device, ctx); return 0; }
        auto **vtable = *reinterpret_cast<void ***>(ctx);
        void *target = vtable[9];
        stage.store("hook_target", std::memory_order_relaxed);
        if (!target || (shader_created && shader_hook_target != target) ||
            (selector_created && engine_base != engine)) {
            doom_invuln_shader_destroy(next); discard_device_objects(stock, device, ctx); return 0;
        }
        stage.store("pin_module", std::memory_order_relaxed);
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                reinterpret_cast<LPCWSTR>(&shader_hook), &self)) {
            doom_invuln_shader_destroy(next); discard_device_objects(stock, device, ctx); return 0;
        }
        stage.store("minhook_initialize", std::memory_order_relaxed);
        MH_STATUS status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            doom_invuln_shader_destroy(next); discard_device_objects(stock, device, ctx); return 0;
        }
        if (!selector_created) {
            stage.store("selector_hook_create", std::memory_order_relaxed);
            void *original = nullptr;
            if (MH_CreateHook(engine + 0x7be9e0, reinterpret_cast<void *>(&selector_hook), &original) != MH_OK) {
                doom_invuln_shader_destroy(next); discard_device_objects(stock, device, ctx); return 0;
            }
            selector_original = reinterpret_cast<Selector>(original); selector_created = true; engine_base = engine;
        }
        if (!shader_created) {
            stage.store("shader_hook_create", std::memory_order_relaxed);
            void *original = nullptr;
            if (MH_CreateHook(target, reinterpret_cast<void *>(&shader_hook), &original) != MH_OK) {
                doom_invuln_shader_destroy(next); discard_device_objects(stock, device, ctx); return 0;
            }
            set_shader_original = reinterpret_cast<SetShader>(original); shader_hook_target = target; shader_created = true;
        }
        context = ctx; context_device = device; stock_shader = stock; replacement = next;
        next_device_check = 0;
        stage.store("selector_hook_enable", std::memory_order_relaxed);
        if (MH_EnableHook(engine + 0x7be9e0) != MH_OK) { (void)doom_invuln_effect_stop(); return 0; }
        selector_enabled = true;
        stage.store("shader_hook_enable", std::memory_order_relaxed);
        if (MH_EnableHook(target) != MH_OK) { (void)doom_invuln_effect_stop(); return 0; }
        shader_enabled = true;
        stage.store("ready", std::memory_order_relaxed);
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { stage.store("start_exception", std::memory_order_relaxed); return 0; }
}

extern "C" int doom_invuln_effect_set(int enabled) {
    if (!selector_enabled || !shader_enabled) return 0;
    if (enabled && GetTickCount64() >= next_device_check) {
        next_device_check = GetTickCount64() + 1000u;
        __try {
            ID3D11Device *device = nullptr;
            ID3D11PixelShader *stock = find_stock_shader(engine_base, &device);
            ID3D11DeviceContext *ctx = nullptr;
            if (device) device->GetImmediateContext(&ctx);
            const bool same = stock == stock_shader && device == context_device && ctx == context;
            discard_device_objects(stock, device, ctx);
            if (!same) { stage.store("device_changed", std::memory_order_relaxed); return 0; }
        } __except(EXCEPTION_EXECUTE_HANDLER) { stage.store("device_exception", std::memory_order_relaxed); return 0; }
    }
    AcquireSRWLockExclusive(&selector_lock);
    const bool was = active.load(std::memory_order_acquire);
    bool ok = true;
    if (enabled && !was) {
        const float one = 1.0f;
        ok = slot_zero(&restore_slot, nullptr) && slot_zero(nullptr, &one);
        if (ok) active.store(true, std::memory_order_release);
    } else if (enabled && was) {
        float current = 0.0f;
        ok = slot_zero(&current, nullptr);
        if (ok && current != 1.0f) {
            const float one = 1.0f;
            restore_slot = current;
            ok = slot_zero(nullptr, &one);
        }
    } else if (!enabled && was) {
        ok = slot_zero(nullptr, &restore_slot);
        if (ok) active.store(false, std::memory_order_release);
    }
    ReleaseSRWLockExclusive(&selector_lock);
    stage.store(ok ? (enabled ? "active" : "ready") : "slot_zero_failed", std::memory_order_relaxed);
    return ok ? 1 : 0;
}

extern "C" void doom_invuln_effect_diagnostics(DoomInvulnEffectDiagnostics *out) {
    if (!out) return;
    out->stage = stage.load(std::memory_order_relaxed);
    out->shader_calls = shader_calls.load(std::memory_order_relaxed);
    out->stock_binds = stock_binds.load(std::memory_order_relaxed);
    out->substitutions = substitutions.load(std::memory_order_relaxed);
    out->selector_calls = selector_calls.load(std::memory_order_relaxed);
}

extern "C" int doom_invuln_effect_stop(void) {
    if (active.load(std::memory_order_acquire) && !doom_invuln_effect_set(0)) return 0;
    if (selector_enabled && MH_DisableHook(engine_base + 0x7be9e0) != MH_OK) return 0;
    selector_enabled = false;
    if (shader_enabled && MH_DisableHook(shader_hook_target) != MH_OK) return 0;
    shader_enabled = false;
    AcquireSRWLockExclusive(&shader_lock);
    if (stock_shader) stock_shader->Release();
    stock_shader = nullptr;
    doom_invuln_shader_destroy(replacement); replacement = nullptr;
    if (context) context->Release();
    if (context_device) context_device->Release();
    context = nullptr; context_device = nullptr; next_device_check = 0;
    ReleaseSRWLockExclusive(&shader_lock);
    return 1;
}
