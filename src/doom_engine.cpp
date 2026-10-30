#include "doom_engine.h"
#include "doom_menu_guard.h"
#include "doom_fpp_body.h"
#include "doom_damage_direction.h"
#include "doom_player_voice.h"
#include "doom_noise.h"
#include "doom_hud_host.h"
#include "doom_viewmodel.h"
#include "doom_combat.h"
#include "doom_deaths.h"
#include "doom_interaction_presentation.h"
#include "doom_world_renderer.h"
#include "doom_death_presentation.h"
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <array>
#include <atomic>
#include <cmath>
#include <climits>
#include <cstring>
#include <mutex>
#include <new>

#if defined(DOOM_MODE_LAB) && (!defined(_DEBUG) || defined(NDEBUG))
#error DOOM_MODE_LAB requires a debug-only build.
#endif

namespace {
constexpr const char *player_hash = "61D9DFB20CFA179C6D097537DA46BA9DBE3D58797938762EF4EACFF5DFCC4B3A";
constexpr const char *game_hash = "CBC7DD7923778936341CDFB755474A0C714079D910849DF16FFC5EC88A103B95";
constexpr const char *engine_hash = "A18CA1103614947AE7FF956C6852F45123B440563A967D9E1E496C42FA574A3C";
constexpr const char *tools_game_hash = "8E19C89C316E319E1E88D0C7A6F6A935C4B9C381905CAB48449CEE8FD6CCBA54";
constexpr const char *tools_engine_hash = "5B8090BFC3D3A25A24DA1846A3AAFD65C97E0D1E700B5DFEBFC7132737B6BFF6";
using Keyboard = char (__fastcall *)(void *, int, unsigned char, unsigned);
using MouseButton = uintptr_t (__fastcall *)(void *, int, int);
using GetPlayer = void *(__fastcall *)(void *);
using GetLevel = void *(__fastcall *)(void *);
struct PlayerPosition { float x, y, z; };
using GetPosition = PlayerPosition *(__fastcall *)(void *, PlayerPosition *);
using HealthSetter = void (__fastcall *)(void *, float, unsigned char);
using HealthGetter = float (__fastcall *)(void *);
using DeadGetter = unsigned char (__fastcall *)(void *);
using MoveAction = void (__fastcall *)(void *, int, float, unsigned char);
using PlayerAction = void (__fastcall *)(void *, int, float, unsigned char);
template<class T> T field(void *p, size_t offset) {
    return *reinterpret_cast<T *>(static_cast<unsigned char *>(p) + offset);
}
bool hash_file(HMODULE module, const char *expected) {
    wchar_t path[32768];
    DWORD len = GetModuleFileNameW(module, path, 32768);
    if (!len || len >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                              nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    std::array<unsigned char, 32> digest{}; std::array<unsigned char, 65536> data{};
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    DWORD got = 0;
    while (ok) {
        if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &got, nullptr)) { ok = false; break; }
        if (!got) break;
        ok = BCryptHashData(hash, data.data(), got, 0) >= 0;
    }
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    CloseHandle(file);
    constexpr char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; ok && i < digest.size(); ++i)
        ok = expected[2*i] == hex[digest[i] >> 4] && expected[2*i+1] == hex[digest[i] & 15];
    return ok;
}
struct Patch {
    void **slot = nullptr; void *original = nullptr; void *hook = nullptr;
    bool install(void **target, void *expected, void *replacement) {
        DWORD old = 0;
        if (*target != expected || !VirtualProtect(target, sizeof(void *), PAGE_READWRITE, &old)) return false;
        void *seen = InterlockedCompareExchangePointer(target, replacement, expected);
        DWORD ignored = 0; bool restored = VirtualProtect(target, sizeof(void *), old, &ignored) != 0;
        if (seen != expected) return false;
        slot = target; original = expected; hook = replacement;
        return restored;
    }
    bool restore() {
        if (!slot) return true;
        DWORD old = 0;
        if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &old)) return false;
        void *seen = InterlockedCompareExchangePointer(slot, original, hook);
        DWORD ignored = 0; bool ok = VirtualProtect(slot, sizeof(void *), old, &ignored) != 0;
        if ((seen != hook && seen != original) || !ok) return false;
        slot = nullptr; return true;
    }
};
}
struct DoomEngine {
    uint32_t capabilities = 0, generation = 0;
    DWORD game_thread = 0;
    unsigned update_depth = 0;
    unsigned char *game = nullptr;
    HMODULE engine_module = nullptr;
    GetLevel get_level = nullptr;
    GetPosition get_position = nullptr;
    GetPosition get_camera_position = nullptr;
    GetPosition get_camera_forward = nullptr;
    DoomCombat *combat = nullptr;
    const char *attack_reason = "not_attempted";
    DoomDeaths *deaths = nullptr;
    DoomInteractionPresentation *presentation = nullptr;
    bool presentation_installed = false;
    bool death_presentation_started = false;
    bool deaths_started = false;
    DoomWorldRenderer *world_renderer = nullptr;
    void *world_renderer_level = nullptr;
    uint32_t world_renderer_generation = 0;
    unsigned world_host_flags = 0;
    bool active = false, published = false;
    std::recursive_mutex mutex;
    Patch keyboard, mouse, health, jump, interaction;
    std::array<bool,256> swallowed{}, forwarded{};
    bool fire = false, keyboard_fire = false, mouse_swallowed = false, mouse_forwarded = false;
    int fire_key = VK_LCONTROL;
    bool interaction_down = false, interaction_failed = false;
    int selected = -1;
    int selected_slot = -1;
    std::array<char,16> cheats{}; unsigned cheat_count = 0;
    DoomPlayer *player = nullptr; DoomEventFn emit = nullptr; void *context = nullptr;
    double damage_carry = 0.0;
    void *level = nullptr;
    void *last_actor = nullptr;
    bool native_alive_known = false, native_alive = false, respawn_pending = false;
    void *death_actor = nullptr;
    bool death_wait = false, death_committed = false;
    bool space_down = false, death_space_armed = false, death_space_request = false;
    DoomHudHost *hud = nullptr;
    bool viewmodel_pending = false;
    bool fpp_body_started = false;
    DoomDamageDirection *damage_direction = nullptr;
    DoomPlayerVoice *player_voice = nullptr;
    DoomNoise *noise = nullptr;
    DoomMovementSampler *movement_sampler = nullptr;
    bool voice_installed = false, voice_replacement = false;
    unsigned damage_sequence = 0;
    int damage_side = 0, damage_amount = 0;
    unsigned damage_thread = 0;
    DoomDamageDirectionSnapshot direction_snapshot{};
    ULONGLONG next_hud_attempt = 0;
    bool health_static_ready = false;
    bool jump_static_ready = false;
    bool parkour_static_ready = false;
    bool stock_profile = false, cheats_enabled = true;
    void *ledge_actor = nullptr;
    bool ledge_hold = false;
    bool interaction_static_ready = false;
    bool player_static_ready = false;
    int classic = 0, bilinear_filtering = 0, dynamic_lights = 1; float incoming = 1, outgoing = 1;
    bool in_update_scope() const { return update_depth && game_thread == GetCurrentThreadId(); }
    void *game_object() const { return game ? *reinterpret_cast<void **>(game + 0x1D07F88) : nullptr; }
    void *local_player() const { void *g = game_object(); return g ? reinterpret_cast<GetPlayer>(game + 0x3C2D90)(g) : nullptr; }
    bool gameplay_view() const {
        void *g = game_object(); if (!g || !local_player()) return false;
        void *input = field<void *>(g,200), *session = field<void *>(g,1288);
        void *manager = session ? field<void *>(session,192) : nullptr;
        return input && field<int>(input,308) == 0 && manager &&
            field<void *>(manager,64) && (field<int>(manager,88) == 1 || field<int>(manager,88) == 2);
    }
    bool gameplay() const {
        DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
        return pid==GetCurrentProcessId() && gameplay_view();
    }
};
static void *current_player_level(DoomEngine *e);
#if defined(DOOM_MODE_LAB)
static void *current_player_camera(DoomEngine *e) {
    __try {
        void *game=e->game_object();
        void *session=game ? field<void *>(game,1288) : nullptr;
        void *manager=session ? field<void *>(session,192) : nullptr;
        return manager ? field<void *>(manager,80) : nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
#endif
static int current_player_vitals(DoomEngine *e, void **actor_out, float *health_out,
                                 bool *alive_out) {
    __try {
        void *actor=e->local_player();
        if (!actor || field<void *>(actor,0)!=e->game+0x17DD738 ||
            field<void *>(actor,24)!=e->game+0x17DE1C8 ||
            field<void *>(actor,40)!=e->game+0x17DE468 ||
            current_player_level(e)!=e->level) return 0;
        float health=reinterpret_cast<HealthGetter>(e->game+0x969DB0)(actor);
        bool alive=health>0.0f &&
            !reinterpret_cast<DeadGetter>(e->game+0x969D20)(actor);
        if (!std::isfinite(health) || health<0 || actor!=e->local_player() ||
            current_player_level(e)!=e->level) return 0;
        *actor_out=actor; *health_out=health; *alive_out=alive;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
namespace {
std::atomic<DoomEngine *> current{nullptr};
Keyboard stock_keyboard = nullptr;
MouseButton stock_mouse = nullptr;
HealthSetter stock_health = nullptr;
MoveAction stock_jump = nullptr;
PlayerAction stock_action = nullptr;
void __fastcall action_hook(void *input, int action, float value, unsigned char flags) {
    DoomEngine *e = current.load(std::memory_order_acquire);
    if (!e) { stock_action(input,action,value,flags); return; }
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (e->active && e->death_wait && value != 0.0f && e->gameplay()) return;
    if (e->active && input && (action == 20 || action == 126 || action == 128)) {
        void *actor = static_cast<unsigned char *>(input) - 0x910;
        bool eligible = !flags && e->gameplay() && actor == e->local_player();
        bool pressed = value >= 1.0f;
        if (eligible && pressed && !e->interaction_down &&
            (!field<void *>(actor,0x14d8) || !field<void *>(actor,0x14e0)))
            e->interaction_failed = true;
        e->interaction_down = eligible && pressed;
    }
    stock_action(input,action,value,flags);
}
void __fastcall jump_hook(void *controller, int action, float value, unsigned char flags) {
    DoomEngine *e = current.load(std::memory_order_acquire);
    if (!e) { stock_jump(controller,action,value,flags); return; }
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (e->active && (e->classic || e->death_wait || e->death_committed) &&
        action == 11 && value == 1.0f && controller &&
        e->gameplay() && reinterpret_cast<GetPlayer>((*reinterpret_cast<void ***>(controller))[4])(controller)
            == e->local_player())
        return;
    stock_jump(controller,action,value,flags);
}
void __fastcall health_hook(void *receiver, float requested, unsigned char flags) {
    const void *caller = _ReturnAddress();
    DoomEngine *e = current.load(std::memory_order_acquire);
    if (!e) { stock_health(receiver, requested, flags); return; }
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active ||
        caller != e->game + 0xB178CA || !e->player || !e->level ||
        current_player_level(e) != e->level) {
        stock_health(receiver, requested, flags); return;
    }
    void *actor = e->local_player();
    if (!actor || receiver != static_cast<unsigned char *>(actor) + 0x918) {
        stock_health(receiver, requested, flags); return;
    }
    const float old_health = reinterpret_cast<HealthGetter>(e->game + 0x969DB0)(actor);
    const float max_health = reinterpret_cast<HealthGetter>(e->game + 0xB17CC0)(actor);
    const float delta = old_health - requested;
    if (!std::isfinite(old_health) || !std::isfinite(requested) ||
        !std::isfinite(max_health) || max_health <= 0 || delta <= 0 ||
        !std::isfinite(e->incoming) || e->incoming < 0) {
        stock_health(receiver, requested, flags); return;
    }
    const double scaled = static_cast<double>(delta) * 100.0 /
                          static_cast<double>(max_health) * e->incoming;
    if (!std::isfinite(scaled) || scaled < 0 || scaled > INT_MAX) {
        stock_health(receiver, requested, flags); return;
    }
    double total = scaled;
    if (e->player->god || e->player->invulnerability_tics || e->player->health <= 0)
        e->damage_carry = 0.0;
    else {
        total += e->damage_carry;
        if (total > INT_MAX) { stock_health(receiver, requested, flags); return; }
        e->damage_carry = total - std::floor(total);
    }
    const int damage = static_cast<int>(std::floor(total));
    const int side=doom_damage_direction_side(e->damage_direction,actor);
    const int applied=doom_damage_from(e->player,damage,side);
    if (applied>0) {
        ++e->damage_sequence; e->damage_side=side; e->damage_amount=applied;
        e->damage_thread=GetCurrentThreadId();
        (void)doom_damage_direction_snapshot(e->damage_direction,&e->direction_snapshot);
    }
    if (e->player->health <= 0 && !e->death_wait && !e->death_committed) {
        e->death_actor=actor;
        e->death_wait=true;
        e->space_down=e->space_down || (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
        e->death_space_armed=!e->space_down;
        e->death_space_request=false;
        e->fire=false; e->keyboard_fire=false;
    }
    /* Keep the live actor at its previous health until the player continues. */
    stock_health(receiver, old_health, flags);
}
char __fastcall keyboard_hook(void *input, int key, unsigned char pressed, unsigned converter) {
    DoomEngine *e = current.load(std::memory_order_acquire);
    if (!e || key < 0 || key > 255) return stock_keyboard(input,key,pressed,converter);
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    const bool eligible = e->active && e->gameplay();
    const int space_scan=static_cast<int>(MapVirtualKeyW(VK_SPACE, MAPVK_VK_TO_VSC_EX) & 0xffU);
    const bool space_key=space_scan && key==space_scan;
    if (space_key) {
        const bool was_down=e->space_down;
        e->space_down=pressed != 0;
        if (e->death_wait) {
            if (!pressed) e->death_space_armed=true;
            else if (eligible && !was_down && e->death_space_armed) {
                e->death_space_request=true;
                e->death_space_armed=false;
            }
        }
    }
    UINT mapped=MapVirtualKeyW(static_cast<UINT>(e->fire_key),MAPVK_VK_TO_VSC_EX);
    int fire_scan=static_cast<int>((mapped & 0xffu) | ((mapped & 0xff00u) ? 0x80u : 0u));
    const bool fire_capture=eligible && mapped && key==fire_scan;
    bool developer_shortcut = false;
#if defined(DOOM_MODE_LAB)
    developer_shortcut = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
#endif
    bool music_digit=false;
    if (eligible && e->cheats_enabled && !e->death_wait && !developer_shortcut &&
        e->player && key>=2 && key<=11) {
        DoomPlayer pending=*e->player;
        for (unsigned i=0; i<e->cheat_count && i<e->cheats.size(); ++i)
            doom_cheat_char(&pending,e->cheats[i],nullptr,nullptr);
        music_digit=doom_cheat_wants_music_digit(&pending)!=0;
    }
    const bool slot_capture=eligible && !e->death_wait && !fire_capture && !developer_shortcut &&
        !music_digit && key >= 2 && key <= 10;
    const bool captured=fire_capture || slot_capture || music_digit || (e->death_wait && eligible && space_key);
    if (!eligible) { e->keyboard_fire=false; e->fire=false; }
    if (fire_capture) e->keyboard_fire=!music_digit && !e->death_wait && !e->death_committed && pressed != 0;
    if (slot_capture && pressed && key <= 8 && !e->swallowed[static_cast<size_t>(key)])
        e->selected_slot = key - 1;
    if (eligible && e->cheats_enabled && pressed && e->cheat_count < e->cheats.size()) {
        UINT vk = MapVirtualKeyW(static_cast<UINT>(key), MAPVK_VSC_TO_VK);
        if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9'))
            e->cheats[e->cheat_count++] = static_cast<char>(vk);
    }
    bool swallow = captured || e->swallowed[static_cast<size_t>(key)];
    if (swallow) {
        if (e->forwarded[static_cast<size_t>(key)]) {
            stock_keyboard(input,key,0,converter);
            e->forwarded[static_cast<size_t>(key)] = false;
        }
        e->swallowed[static_cast<size_t>(key)] = pressed != 0; return 0;
    }
    e->forwarded[static_cast<size_t>(key)] = pressed != 0;
    return stock_keyboard(input,key,pressed,converter);
}
uintptr_t __fastcall mouse_hook(void *input, int button, int pressed) {
    DoomEngine *e = current.load(std::memory_order_acquire);
    if (!e) return stock_mouse(input,button,pressed);
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (button == 0xF000) {
        bool capture = e->active && e->gameplay();
        if (capture || e->mouse_swallowed) {
            if (e->mouse_forwarded) {
                stock_mouse(input,button,0);
                e->mouse_forwarded = false;
            }
            e->fire = capture && !e->death_wait && !e->death_committed && pressed != 0;
            e->mouse_swallowed = pressed != 0;
            return 0;
        }
        e->mouse_forwarded = pressed != 0;
    }
    return stock_mouse(input,button,pressed);
}
}
extern "C" DoomEngine *doom_engine_create(void) { return new (std::nothrow) DoomEngine; }
extern "C" void doom_engine_destroy(DoomEngine *engine) {
    if (!engine) return;
    doom_engine_deactivate(engine);
    doom_player_voice_destroy(engine->player_voice);
    engine->player_voice=nullptr;
    doom_noise_destroy(engine->noise); engine->noise=nullptr;
    doom_movement_sampler_destroy(engine->movement_sampler); engine->movement_sampler=nullptr;
    doom_deaths_destroy(engine->deaths);
    engine->deaths = nullptr;
    doom_interaction_presentation_destroy(engine->presentation);
    engine->presentation = nullptr;
    doom_combat_destroy(engine->combat);
    engine->combat = nullptr;
    if (!engine->published) delete engine;
}
extern "C" uint32_t doom_engine_capabilities(const DoomEngine *engine) { return engine ? engine->capabilities : 0; }
extern "C" int doom_engine_begin_update(DoomEngine *e) {
    if (!e) return 0;
    try {
        e->mutex.lock();
        if (!e->update_depth) e->game_thread=GetCurrentThreadId();
        ++e->update_depth;
#if defined(DOOM_MODE_LAB)
        if (!e->active && e->viewmodel_pending && doom_viewmodel_stop()) e->viewmodel_pending=false;
        if (!e->active && e->fpp_body_started && doom_fpp_body_stop()) e->fpp_body_started=false;
        if (!e->active && e->damage_direction && doom_damage_direction_stop(e->damage_direction))
            e->damage_direction=nullptr;
        if (!e->active && e->voice_installed && doom_player_voice_uninstall(e->player_voice))
            e->voice_installed=false;
#endif
        return 1;
    } catch (...) { return 0; }
}
extern "C" void doom_engine_end_update(DoomEngine *e) {
    if (!e || !e->update_depth || e->game_thread!=GetCurrentThreadId()) return;
    if (!--e->update_depth) {
        doom_combat_world_unbind(e->combat);
        e->world_host_flags = 0;
        e->game_thread=0;
    }
    e->mutex.unlock();
}
static void *current_player_level(DoomEngine *e) {
    void *level = nullptr;
    __try {
        void *player = e->local_player();
        if (player && field<void *>(player,0) == e->game + 0x17DD738 &&
            field<void *>(player,40) == e->game + 0x17DE468) {
            level = e->get_level(static_cast<unsigned char *>(player)+40);
            if (player != e->local_player() ||
                level != e->get_level(static_cast<unsigned char *>(player)+40)) level = nullptr;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { level = nullptr; }
    return level;
}
static void *owned_controller(DoomEngine *e,void *actor,size_t offset,unsigned vtable_rva) {
    void *controller=field<void *>(actor,offset);
    if (!controller || field<void *>(controller,0)!=e->game+vtable_rva) return nullptr;
    auto **v=*reinterpret_cast<void ***>(controller);
    if (v[4]!=e->game+0x106b80 || reinterpret_cast<GetPlayer>(v[4])(controller)!=actor) return nullptr;
    return controller;
}
static int current_parkour(DoomEngine *e, void *actor,DoomEngineFrame *frame) {
    if (!e->parkour_static_ready || !actor || !e->in_update_scope()) return 0;
    __try {
        if (actor!=e->local_player() || current_player_level(e)!=e->level) return 0;
        if (e->ledge_actor!=actor) { e->ledge_actor=actor; e->ledge_hold=false; }
        void *move=owned_controller(e,actor,0xbc0,0x180b368);
        void *edge=owned_controller(e,actor,0xc80,0x1808d88);
        void *ladder=owned_controller(e,actor,0xbf8,0x180a4c8);
        frame->move_state=move ? field<uint32_t>(move,0x1c0) : 0;
        frame->edge_active=edge && field<unsigned char>(edge,0x10)!=0;
        frame->edge_state=edge ? field<uint32_t>(edge,0x1fc) : 0;
        frame->ladder_active=ladder && field<unsigned char>(ladder,0x10)!=0;
        if (actor!=e->local_player() || current_player_level(e)!=e->level) return 0;
        if (frame->edge_active) {
            const unsigned state=frame->edge_state;
            if (state==1 || state==2 || state==3 || state==11 || state==12 || state==14 || state==15)
                e->ledge_hold=true;
            return e->ledge_hold ? 2 : 1;
        }
        e->ledge_hold=false;
        if (frame->ladder_active) return 1;
        return frame->move_state>=1 && frame->move_state<=23 ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" void *doom_engine_level(DoomEngine *e) {
    if (!e || !e->game || !e->get_level) return nullptr;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->in_update_scope()) return nullptr;
    return current_player_level(e);
}
static int current_player_position(DoomEngine *e, float *x, float *y, float *z) {
    __try {
        void *game = e->game_object();
        if (!game || field<void *>(game,0) != e->game+0x157D5F8) return 0;
        void *session = field<void *>(game,0x508);
        if (!session) return 0;
        void *session_vtable = field<void *>(session,0);
        if (session_vtable != e->game+0x1591FC8 &&
            session_vtable != e->game+0x158E488 &&
            session_vtable != e->game+0x158F838) return 0;
        void *owner = field<void *>(session,0xB8);
        if (!owner || !field<void *>(owner,0x50)) return 0;
        void *actor = e->local_player();
        if (!actor || actor != field<void *>(owner,0x58) ||
            field<void *>(actor,0) != e->game+0x17DD738 ||
            field<void *>(actor,24) != e->game+0x17DE1C8 ||
            field<void *>(actor,40) != e->game+0x17DE468) return 0;
        PlayerPosition value{};
        PlayerPosition *result = e->get_position(static_cast<unsigned char *>(actor)+24,&value);
        if (result!=&value || !std::isfinite(result->x) || !std::isfinite(result->y) ||
            !std::isfinite(result->z) || game != e->game_object() ||
            session != field<void *>(game,0x508) || owner != field<void *>(session,0xB8) ||
            actor != e->local_player() ||
            current_player_level(e) != e->level) return 0;
        *x=result->x; *y=result->y; *z=result->z;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" int doom_engine_position(DoomEngine *e, float *x, float *y, float *z) {
    if (!e || !x || !y || !z) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->get_position ||
        !e->level || current_player_level(e) != e->level) return 0;
    return current_player_position(e,x,y,z);
}
static int current_camera_position(DoomEngine *e, float *x, float *y, float *z) {
    __try {
        void *game=e->game_object();
        if (!game || field<void *>(game,0)!=e->game+0x157D5F8) return 0;
        void *session=field<void *>(game,0x508);
        if (!session) return 0;
        void *session_vtable=field<void *>(session,0);
        if (session_vtable!=e->game+0x1591FC8 && session_vtable!=e->game+0x158E488 &&
            session_vtable!=e->game+0x158F838) return 0;
        void *manager=field<void *>(session,192);
        if (!manager || (field<int>(manager,88)!=1 && field<int>(manager,88)!=2)) return 0;
        void *camera=field<void *>(manager,80);
        if (!camera) return 0;
        PlayerPosition value{};
        PlayerPosition *result=e->get_camera_position(camera,&value);
        if (!result || !std::isfinite(result->x) || !std::isfinite(result->y) ||
            !std::isfinite(result->z) || game!=e->game_object() ||
            session!=field<void *>(game,0x508) || manager!=field<void *>(session,192) ||
            camera!=field<void *>(manager,80) || current_player_level(e)!=e->level) return 0;
        *x=result->x; *y=result->y; *z=result->z;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" int doom_engine_camera_position(DoomEngine *e, float *x, float *y, float *z) {
    if (!e || !x || !y || !z) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->get_camera_position ||
        !e->level || current_player_level(e)!=e->level) return 0;
    return current_camera_position(e,x,y,z);
}
static int current_shot_basis(DoomEngine *e, DoomVec3 *origin, DoomVec3 *direction) {
    __try {
        void *game=e->game_object();
        if (!game || field<void *>(game,0)!=e->game+0x157D5F8) return 0;
        void *session=field<void *>(game,0x508);
        if (!session) return 0;
        void *session_vtable=field<void *>(session,0);
        if (session_vtable!=e->game+0x1591FC8 && session_vtable!=e->game+0x158E488 &&
            session_vtable!=e->game+0x158F838) return 0;
        void *manager=field<void *>(session,192);
        if (!manager || (field<int>(manager,88)!=1 && field<int>(manager,88)!=2)) return 0;
        void *camera=field<void *>(manager,80);
        void *player=e->local_player();
        if (!camera || !player || current_player_level(e)!=e->level) return 0;
        PlayerPosition position{}, forward{};
        PlayerPosition *p=e->get_camera_position(camera,&position);
        PlayerPosition *f=e->get_camera_forward(camera,&forward);
        if (!p || !f || !std::isfinite(p->x) || !std::isfinite(p->y) || !std::isfinite(p->z) ||
            !std::isfinite(f->x) || !std::isfinite(f->y) || !std::isfinite(f->z)) return 0;
        const float magnitude=std::sqrt(f->x*f->x+f->y*f->y+f->z*f->z);
        if (!std::isfinite(magnitude) || magnitude<0.0001f || game!=e->game_object() ||
            session!=field<void *>(game,0x508) || manager!=field<void *>(session,192) ||
            camera!=field<void *>(manager,80) || player!=e->local_player() ||
            current_player_level(e)!=e->level) return 0;
        *origin={p->x*DOOM_WORLD_UNITS_PER_METRE,p->z*DOOM_WORLD_UNITS_PER_METRE,
                 p->y*DOOM_WORLD_UNITS_PER_METRE};
        *direction={-f->x/magnitude,-f->z/magnitude,-f->y/magnitude};
        if (!std::isfinite(origin->x) || !std::isfinite(origin->y) || !std::isfinite(origin->z))
            return 0;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" int doom_engine_shot_basis(DoomEngine *e, DoomVec3 *origin, DoomVec3 *direction) {
    if (!e || !origin || !direction) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->get_camera_position ||
        !e->get_camera_forward || !e->level || !e->gameplay()) return 0;
    return current_shot_basis(e,origin,direction);
}
#if defined(DOOM_MODE_LAB)
static int damage_view_read(DoomEngine *e,void **player,void **camera) {
    __try {
        if (!e->active || !e->level || current_player_level(e)!=e->level) return 0;
        void *actor=e->local_player();
        if (!actor || field<void *>(actor,0)!=e->game+0x17dd738) return 0;
        void *game=e->game_object();
        void *session=game ? field<void *>(game,1288) : nullptr;
        void *manager=session ? field<void *>(session,192) : nullptr;
        if (!manager || (field<int>(manager,88)!=1 && field<int>(manager,88)!=2)) return 0;
        void *view=field<void *>(manager,80);
        if (!view || e->local_player()!=actor || current_player_level(e)!=e->level) return 0;
        *player=actor; *camera=view; return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static int damage_view(void *context,void **player,void **camera) {
    auto *e=static_cast<DoomEngine *>(context);
    if (!e || !player || !camera || GetCurrentThreadId()!=e->game_thread) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    return damage_view_read(e,player,camera);
}
#endif
extern "C" unsigned doom_engine_death_drain(DoomEngine *e, DoomDeathRecord *out, unsigned capacity) {
#if defined(DOOM_MODE_LAB)
    if (!e || !out || !capacity) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->deaths_started || !e->in_update_scope() || !e->level) return 0;
    void *actor=nullptr; float health=0; bool alive=false;
    if (!current_player_vitals(e,&actor,&health,&alive)) {
        doom_deaths_context(e->deaths,nullptr,nullptr,e->generation);
        return 0;
    }
    doom_deaths_context(e->deaths,actor,e->level,e->generation);
    return doom_deaths_drain(e->deaths,out,capacity);
#else
    (void)e; (void)out; (void)capacity;
    return 0;
#endif
}
extern "C" void doom_engine_bind(DoomEngine *e, DoomPlayer *p, DoomEventFn fn, void *ctx) {
    if (e) { e->player=p; e->emit=fn; e->context=ctx; }
}
extern "C" void doom_engine_reset_damage(DoomEngine *e) {
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    e->damage_carry = 0.0;
}
extern "C" void doom_engine_configure(DoomEngine *e, int classic, float incoming, float outgoing) {
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    e->classic=classic; e->incoming=incoming; e->outgoing=outgoing;
}
extern "C" void doom_engine_set_texture_filtering(DoomEngine *e, int bilinear) {
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    e->bilinear_filtering=bilinear ? 1 : 0;
}
extern "C" int doom_engine_texture_filtering(DoomEngine *e) {
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    return e->bilinear_filtering;
}
extern "C" void doom_engine_set_dynamic_lights(DoomEngine *e, int enabled) {
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    e->dynamic_lights=enabled ? 1 : 0;
}
extern "C" int doom_engine_dynamic_lights(DoomEngine *e) {
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    return e->dynamic_lights;
}
extern "C" void doom_engine_set_fire_key(DoomEngine *e, int virtual_key) {
    if (!e || virtual_key < 1 || virtual_key > 254) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (e->fire_key != virtual_key) {
        e->keyboard_fire = false;
        e->fire_key = virtual_key;
    }
}
extern "C" void doom_engine_set_cheats_enabled(DoomEngine *e, int enabled) {
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    const bool value=enabled!=0;
    if (value!=e->cheats_enabled) { e->cheat_count=0; e->cheats.fill(0); }
    e->cheats_enabled=value;
}
extern "C" void doom_engine_set_voice_replacement(DoomEngine *e, int ready) {
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    e->voice_replacement=ready!=0;
    if (e->voice_installed) doom_player_voice_set_active(e->player_voice,e->active && e->voice_replacement);
}
extern "C" int doom_engine_initialize(DoomEngine *e) {
    if (!e) return 0;
    HMODULE exe=GetModuleHandleW(nullptr), game=GetModuleHandleW(L"gamedll_x64_rwdi.dll"),
        engine=GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!game || !engine || !hash_file(exe,player_hash)) return 0;
    bool stock = hash_file(game,game_hash) && hash_file(engine,engine_hash);
    bool tools = !stock && hash_file(game,tools_game_hash) && hash_file(engine,tools_engine_hash);
    if (!stock && !tools) return 0;
    e->stock_profile=stock;
    e->game = reinterpret_cast<unsigned char *>(game);
    e->engine_module = engine;
#if defined(DOOM_MODE_LAB)
    if (stock && !e->player_voice) e->player_voice=doom_player_voice_create(game);
    if (!e->noise) e->noise=doom_noise_create(game,engine);
    wchar_t movementSamples[8]{};
    if (stock && !e->movement_sampler && GetEnvironmentVariableW(L"DOOMING_LIGHT_MOVEMENT_SAMPLES",movementSamples,8)==1 && movementSamples[0]==L'1')
        e->movement_sampler=doom_movement_sampler_create(game,engine,1);
#endif
    e->get_level = reinterpret_cast<GetLevel>(GetProcAddress(engine,
        "?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    e->get_position = reinterpret_cast<GetPosition>(GetProcAddress(engine,
        "?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ"));
    e->get_camera_position = reinterpret_cast<GetPosition>(GetProcAddress(engine,
        "?GetPosition@IBaseCamera@@QEBA?BVvec3@@XZ"));
    e->get_camera_forward = reinterpret_cast<GetPosition>(GetProcAddress(engine,
        "?GetForwardVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
    if (!e->combat) e->combat = doom_combat_create(game,engine);
#if defined(DOOM_MODE_LAB)
    if (!e->deaths) e->deaths = doom_deaths_create(game,engine);
    if (!e->presentation) e->presentation = doom_interaction_presentation_create(game);
#endif
    auto table = reinterpret_cast<void **>(e->game + 0x18FFD78);
    static const unsigned char get_player_prefix[] =
        {0x48,0x8b,0x81,0x08,0x05,0x00,0x00,0x48,0x85,0xc0,0x74,0x18,0x48,0x8b,0x80,0xb8};
    e->player_static_ready = !std::memcmp(e->game+0x3C2D90,get_player_prefix,sizeof(get_player_prefix));
    if (table[144] == e->game + 0x10C6D10 && table[146] == e->game + 0x10C7390)
        e->capabilities |= DOOM_ENGINE_INPUT;
    static const unsigned char setter_prefix[] =
        {0x48,0x89,0x5c,0x24,0x10,0x56,0x48,0x83,0xec,0x50,0x48,0x8b,0x01,0x0f,0x29,0x74};
    static const unsigned char getter_prefix[] =
        {0xf3,0x0f,0x10,0x81,0x9c,0x12,0x00,0x00,0xc3};
    static const unsigned char acting_dead_prefix[] =
        {0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xd9,0xff,0x90,0x28,0x01,0x00,0x00};
    static const unsigned char dead_prefix[] =
        {0x48,0x83,0xb9,0x70,0x2f,0x00,0x00,0x00,0x74,0x0a,0x48,0x83,0xb9,0x78,0x2f,0x00,0x00,0x00,0x75,0x26};
    static const unsigned char max_prefix[] =
        {0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xf9};
    static const unsigned char commit[] =
        {0xf3,0x0f,0x5c,0x44,0x24,0x78,0x0f,0x28,0xc8,0xff,0x53,0x20};
    auto health_table = reinterpret_cast<void **>(e->game + 0x17DEFB8);
    auto player_table = reinterpret_cast<void **>(e->game + 0x17DD738);
    e->health_static_ready = health_table[4] == e->game + 0xB1EED0 &&
        player_table[0x128 / sizeof(void *)] == e->game + 0xB41A30 &&
        !std::memcmp(e->game+0xB1EED0,setter_prefix,sizeof(setter_prefix)) &&
        !std::memcmp(e->game+0x969DB0,getter_prefix,sizeof(getter_prefix)) &&
        !std::memcmp(e->game+0x969D20,acting_dead_prefix,sizeof(acting_dead_prefix)) &&
        !std::memcmp(e->game+0xB41A30,dead_prefix,sizeof(dead_prefix)) &&
        !std::memcmp(e->game+0xB17CC0,max_prefix,sizeof(max_prefix)) &&
        !std::memcmp(e->game+0xB178BE,commit,sizeof(commit));
    static const unsigned char jump_prefix[] =
        {0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x80};
    auto move_table = reinterpret_cast<void **>(e->game + 0x180B368);
    e->jump_static_ready = move_table[22] == e->game + 0xCD92D0 &&
        !std::memcmp(e->game+0xCD92D0,jump_prefix,sizeof(jump_prefix));
    e->parkour_static_ready=stock && e->jump_static_ready;
    static const unsigned char action_prefix[] =
        {0x48,0x8b,0xc4,0x55,0x53,0x57,0x41,0x54,0x48,0x8d,0x68,0xa1,0x48,0x81,0xec,0xc8};
    auto action_table = reinterpret_cast<void **>(e->game + 0x17DEF68);
    e->interaction_static_ready = action_table[6] == e->game + 0xB090D0 &&
        !std::memcmp(e->game+0xB090D0,action_prefix,sizeof(action_prefix));
    /* The hook remains gated until health scaling and native side effects have
       been observed on both supported Player routes. */
#if defined(DOOM_MODE_LAB)
    return e->player_static_ready && e->get_level && e->get_position && e->combat &&
        (e->capabilities & DOOM_ENGINE_INPUT) && e->health_static_ready &&
        e->jump_static_ready && e->interaction_static_ready;
#else
    return (e->capabilities & DOOM_ENGINE_REQUIRED) == DOOM_ENGINE_REQUIRED;
#endif
}
extern "C" int doom_engine_activate(DoomEngine *e, uint32_t generation) {
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->in_update_scope()) return 0;
#if defined(DOOM_MODE_LAB)
    if (e->death_presentation_started) {
        if (!doom_death_presentation_stop()) return 0;
        e->death_presentation_started=false;
    }
    if (e->world_renderer) {
        void *old_level=current_player_level(e);
        if (!old_level || old_level!=e->world_renderer_level ||
            !doom_world_renderer_destroy(e->world_renderer,old_level,e->world_renderer_generation))
            return 0;
        e->world_renderer=nullptr; e->world_renderer_level=nullptr; e->world_renderer_generation=0;
    }
    if (e->viewmodel_pending) {
        if (!doom_viewmodel_stop()) return 0;
        e->viewmodel_pending=false;
    }
    if (e->fpp_body_started) {
        if (!doom_fpp_body_stop()) return 0;
        e->fpp_body_started=false;
    }
    if (e->damage_direction) {
        if (!doom_damage_direction_stop(e->damage_direction)) return 0;
        e->damage_direction=nullptr;
    }
    if (e->voice_installed) {
        if (!doom_player_voice_uninstall(e->player_voice)) return 0;
        e->voice_installed=false;
    }
    if (e->presentation_installed) {
        if (!doom_interaction_presentation_uninstall(e->presentation)) return 0;
        e->presentation_installed = false;
    }
    if (e->deaths_started) {
        if (!doom_deaths_stop(e->deaths)) return 0;
        e->deaths_started = false;
    }
    if (!e->player_static_ready || !e->get_level || !e->get_position || !e->combat ||
        !(e->capabilities & DOOM_ENGINE_INPUT) || !e->health_static_ready ||
        !e->jump_static_ready || !e->interaction_static_ready) return 0;
#else
    if ((e->capabilities & DOOM_ENGINE_REQUIRED) != DOOM_ENGINE_REQUIRED) return 0;
#endif
    auto table = reinterpret_cast<void **>(e->game + 0x18FFD78);
    stock_keyboard = reinterpret_cast<Keyboard>(e->game + 0x10C6D10);
    stock_mouse = reinterpret_cast<MouseButton>(e->game + 0x10C7390);
    stock_health = reinterpret_cast<HealthSetter>(e->game + 0xB1EED0);
    stock_jump = reinterpret_cast<MoveAction>(e->game + 0xCD92D0);
    stock_action = reinterpret_cast<PlayerAction>(e->game + 0xB090D0);
    DoomEngine *empty = nullptr;
    if (!current.compare_exchange_strong(empty,e)) return 0;
    HMODULE self = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&keyboard_hook), &self)) {
        current.store(nullptr, std::memory_order_release); return 0;
    }
    e->published = true;
    if (!e->health_static_ready || !e->jump_static_ready || !e->interaction_static_ready ||
        !e->health.install(reinterpret_cast<void **>(e->game + 0x17DEFB8)+4,
            reinterpret_cast<void *>(stock_health),reinterpret_cast<void *>(health_hook)) ||
        !e->jump.install(reinterpret_cast<void **>(e->game + 0x180B368)+22,
            reinterpret_cast<void *>(stock_jump),reinterpret_cast<void *>(jump_hook)) ||
        !e->interaction.install(reinterpret_cast<void **>(e->game + 0x17DEF68)+6,
            reinterpret_cast<void *>(stock_action),reinterpret_cast<void *>(action_hook)) ||
        !e->keyboard.install(table+144,reinterpret_cast<void *>(stock_keyboard),reinterpret_cast<void *>(keyboard_hook)) ||
        !e->mouse.install(table+146,reinterpret_cast<void *>(stock_mouse),reinterpret_cast<void *>(mouse_hook))) {
        doom_engine_deactivate(e); return 0;
    }
    {
        e->generation=generation; e->damage_carry=0.0;
        e->damage_sequence=0; e->damage_side=0; e->damage_amount=0;
        e->last_actor=nullptr; e->native_alive_known=false; e->native_alive=false;
        e->respawn_pending=false;
        e->death_actor=nullptr; e->death_wait=false; e->death_committed=false;
        e->death_space_armed=false; e->death_space_request=false;
        e->level=current_player_level(e); e->active=e->level != nullptr;
    }
    if (!e->active) { doom_engine_deactivate(e); return 0; }
#if defined(DOOM_MODE_LAB)
    if (!doom_menu_guard_start(e->game,e->stock_profile ? 1 : 0))
        OutputDebugStringA("Doom Mode Lab: player menu control unavailable\n");
    e->death_presentation_started=doom_death_presentation_start(e->game,e->engine_module)!=0;
    if (!e->death_presentation_started)
        OutputDebugStringA("Doom Mode Lab: death camera unavailable\n");
    if (doom_viewmodel_start(e->game, GetModuleHandleW(L"engine_x64_rwdi.dll"))) e->viewmodel_pending=true;
    else OutputDebugStringA("Doom Mode Lab: native weapon visibility unavailable\n");
    e->fpp_body_started=doom_fpp_body_start(e->game,e->engine_module)!=0;
    if (!e->fpp_body_started) OutputDebugStringA("Doom Mode Lab: player body visibility unavailable\n");
    e->damage_direction=doom_damage_direction_start(e->game,e->engine_module,damage_view,e);
    if (!e->damage_direction) OutputDebugStringA("Doom Mode Lab: damage direction unavailable\n");
    if (e->player_voice && e->voice_replacement && doom_player_voice_install(e->player_voice)) {
        e->voice_installed=true;
        doom_player_voice_set_active(e->player_voice,1);
    }
    if (e->presentation && doom_interaction_presentation_install(e->presentation)) {
        e->presentation_installed = true;
        doom_interaction_presentation_set_active(e->presentation, 1);
    } else OutputDebugStringA("Doom Mode Lab: interaction look control unavailable\n");
    if (e->deaths && doom_deaths_start(e->deaths)) {
        e->deaths_started = true;
        doom_deaths_context(e->deaths, e->local_player(), e->level, e->generation);
    } else OutputDebugStringA("Doom Mode Lab: enemy death observer unavailable\n");
#endif
    return 1;
}
extern "C" void doom_engine_deactivate(DoomEngine *e) {
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    (void)doom_menu_guard_stop();
    e->ledge_actor=nullptr; e->ledge_hold=false;
    if (e->voice_installed) {
        doom_player_voice_set_active(e->player_voice,0);
        if (doom_player_voice_uninstall(e->player_voice)) e->voice_installed=false;
    }
    if (e->damage_direction && doom_damage_direction_stop(e->damage_direction))
        e->damage_direction=nullptr;
    if (e->fpp_body_started && e->in_update_scope() && doom_fpp_body_stop())
        e->fpp_body_started=false;
#if defined(DOOM_MODE_LAB)
    if (e->death_presentation_started && e->in_update_scope() &&
        doom_death_presentation_stop()) e->death_presentation_started=false;
    if (e->world_renderer && e->in_update_scope() && e->level &&
        e->level == e->world_renderer_level &&
        current_player_level(e) == e->world_renderer_level &&
        doom_world_renderer_destroy(e->world_renderer, e->level, e->world_renderer_generation)) {
        e->world_renderer = nullptr;
        e->world_renderer_level = nullptr;
        e->world_renderer_generation = 0;
    }
    if (e->presentation_installed) {
        doom_interaction_presentation_set_active(e->presentation, 0);
        if (doom_interaction_presentation_uninstall(e->presentation))
            e->presentation_installed = false;
    }
    if (e->deaths_started && doom_deaths_stop(e->deaths)) e->deaths_started = false;
    if (e->viewmodel_pending) {
        (void)doom_viewmodel_suspend();
        if (e->in_update_scope() && doom_viewmodel_stop()) e->viewmodel_pending=false;
    }
#endif
    if (e->hud && e->in_update_scope()) {
        void *level = current_player_level(e);
#if defined(DOOM_MODE_LAB)
        doom_hud_host_death_screen(e->hud, level, e->generation, 0);
#endif
        doom_hud_host_destroy(e->hud, level, e->generation);
        e->hud = nullptr;
    }
    doom_combat_world_unbind(e->combat);
    e->world_host_flags=0;
    e->active=false; e->fire=false; e->keyboard_fire=false;
    e->level=nullptr; e->damage_carry=0.0;
    e->last_actor=nullptr; e->native_alive_known=false; e->native_alive=false;
    e->respawn_pending=false;
    e->death_actor=nullptr; e->death_wait=false; e->death_committed=false;
    e->death_space_armed=false; e->death_space_request=false; e->space_down=false;
    e->swallowed.fill(false); e->forwarded.fill(false);
    e->mouse_swallowed=false; e->mouse_forwarded=false;
    e->interaction_down=false; e->interaction_failed=false;
    e->next_hud_attempt=0;
    e->selected=-1; e->selected_slot=-1; e->cheat_count=0; e->cheats.fill(0);
    bool ok=e->mouse.restore(); ok=e->keyboard.restore() && ok;
    ok=e->interaction.restore() && ok; ok=e->jump.restore() && ok;
    ok=e->health.restore() && ok;
    if (ok) { DoomEngine *expected=e; current.compare_exchange_strong(expected,nullptr); }
}
extern "C" int doom_engine_poll(DoomEngine *e, DoomEngineFrame *frame) {
    if (!frame) return 0;
    std::memset(frame,0,sizeof(*frame)); frame->weapon_select=-1; frame->weapon_slot=-1;
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active) return 0;
    if (e->player && (e->player->god || e->player->invulnerability_tics || e->player->health <= 0))
        e->damage_carry=0.0;
    if (!e->in_update_scope() || !e->level) return 0;
    frame->map_generation=e->generation;
    void *actor=nullptr; float native_health=0.0f; bool native_alive=false;
    const bool has_vitals=current_player_vitals(e,&actor,&native_health,&native_alive)!=0;
#if defined(DOOM_MODE_LAB)
    if (e->deaths_started)
        doom_deaths_context(e->deaths, has_vitals ? actor : nullptr,
            has_vitals ? e->level : nullptr, e->generation);
#endif
    if (has_vitals) {
        if (e->death_actor && actor != e->death_actor) {
            e->death_actor=nullptr; e->death_wait=false; e->death_committed=false;
            e->death_space_armed=false; e->death_space_request=false;
        }
        if (e->native_alive_known && native_alive &&
            (!e->native_alive || (e->last_actor && actor!=e->last_actor))) {
            e->respawn_pending=true;
            e->damage_carry=0.0;
            e->damage_sequence=0; e->damage_side=0; e->damage_amount=0;
        }
        e->last_actor=actor; e->native_alive=native_alive; e->native_alive_known=true;
    }
    frame->paused=!has_vitals || !e->gameplay();
    const bool gameplayView=has_vitals && e->gameplay_view();
#if defined(DOOM_MODE_LAB)
    const int suppressBody=gameplayView && native_alive;
    frame->body_suppression=e->fpp_body_started && doom_fpp_body_update(suppressBody) ? suppressBody : -1;
    if (e->voice_installed)
        doom_player_voice_set_active(e->player_voice,!frame->paused && native_alive && e->voice_replacement);
#endif
    frame->damage_sequence=e->damage_sequence;
    frame->damage_side=e->damage_side;
    frame->damage_amount=e->damage_amount;
    frame->damage_thread=e->damage_thread; frame->update_thread=e->game_thread;
    frame->direction=e->direction_snapshot;
    frame->parkour=!frame->paused && native_alive ? current_parkour(e,actor,frame) : 0;
    if (frame->paused) {
        e->fire=false; e->keyboard_fire=false;
        e->death_space_request=false;
    }
    if (frame->paused && has_vitals && e->hud && !gameplayView)
        doom_hud_host_hide(e->hud, e->level, e->generation);
#if defined(DOOM_MODE_LAB)
    if (e->death_committed && e->hud)
        doom_hud_host_death_screen(e->hud, e->level, e->generation, 1);
    if (e->death_presentation_started) {
        doom_death_presentation_set(has_vitals ? current_player_camera(e) : nullptr,
            has_vitals ? actor : nullptr, e->generation,
            !frame->paused && e->death_wait, e->player ? e->player->view_height : 41);
    }
#endif
    if (!frame->paused && e->death_wait && e->death_space_request &&
        has_vitals && native_alive && actor == e->death_actor &&
        current_player_level(e) == e->level) {
        bool commit_ready=true;
#if defined(DOOM_MODE_LAB)
        if (e->death_presentation_started) {
            doom_death_presentation_set(current_player_camera(e), actor, e->generation, 0, 41);
            if (!doom_death_presentation_stop()) commit_ready=false;
            else e->death_presentation_started=false;
        }
        if (commit_ready && e->hud)
            doom_hud_host_death_screen(e->hud, e->level, e->generation, 1);
#endif
        if (commit_ready) {
        e->death_space_request=false;
        e->death_wait=false;
        e->death_committed=true;
        /* This is the exact-profile native health setter, called once from
         * the owned game-frame route for the same live actor. */
        stock_health(static_cast<unsigned char *>(actor)+0x918, 0.0f, 1);
        frame->death_commit=1;
        native_alive=false;
        e->last_actor=actor; e->native_alive=false; e->native_alive_known=true;
        }
    }
    frame->doom_dead=e->death_wait ? 1 : 0;
    frame->ready=1; frame->alive=has_vitals ? native_alive : e->native_alive;
    if (!frame->paused && e->respawn_pending) {
#if defined(DOOM_MODE_LAB)
        if (e->hud)
            doom_hud_host_death_screen(e->hud, e->level, e->generation, 0);
#endif
        e->death_actor=nullptr; e->death_wait=false; e->death_committed=false;
        e->death_space_armed=false; e->death_space_request=false;
#if defined(DOOM_MODE_LAB)
        if (!e->death_presentation_started)
            e->death_presentation_started=doom_death_presentation_start(e->game,e->engine_module)!=0;
#endif
        frame->respawn=1; e->respawn_pending=false;
    }
    frame->fire=!frame->paused && !e->death_wait && (e->fire || e->keyboard_fire);
    frame->weapon_select=e->selected; e->selected=-1;
    frame->weapon_slot=e->selected_slot; e->selected_slot=-1;
    frame->interact_failed=e->interaction_failed && !frame->paused;
    e->interaction_failed=false;
    frame->cheat_count=e->cheat_count;
    std::memcpy(frame->cheat_chars,e->cheats.data(),e->cheat_count); e->cheat_count=0;
    return 1;
}
extern "C" int doom_engine_attack(DoomEngine *e, const DoomEvent *event) {
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!event) { e->attack_reason="event_missing"; return 0; }
    if (!e->active) { e->attack_reason="engine_inactive"; return 0; }
    if (!e->combat) { e->attack_reason="combat_missing"; return 0; }
    if (!e->in_update_scope()) { e->attack_reason="outside_update_scope"; return 0; }
    if (!e->level) { e->attack_reason="level_missing"; return 0; }
    if (current_player_level(e) != e->level) { e->attack_reason="player_level_mismatch"; return 0; }
    if (!e->gameplay()) { e->attack_reason="gameplay_unavailable"; return 0; }
    void *g=e->game_object();
    if (!g) { e->attack_reason="game_missing"; return 0; }
    void *session=g ? field<void *>(g,1288) : nullptr;
    if (!session) { e->attack_reason="session_missing"; return 0; }
    void *manager=session ? field<void *>(session,192) : nullptr;
    if (!manager) { e->attack_reason="camera_manager_missing"; return 0; }
    if (field<int>(manager,88)!=1 && field<int>(manager,88)!=2) { e->attack_reason="camera_mode_unsupported"; return 0; }
    void *camera=field<void *>(manager,80);
    void *actor=e->local_player();
    if (!camera) { e->attack_reason="camera_missing"; return 0; }
    if (!actor) { e->attack_reason="actor_missing"; return 0; }
    const int result=doom_combat_hitscan(e->combat,actor,e->level,camera,event,e->outgoing);
    e->attack_reason=doom_combat_last_reason(e->combat);
    return result;
}
extern "C" const char *doom_engine_attack_reason(DoomEngine *e) {
    if (!e) return "engine_missing";
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    return e->attack_reason;
}
extern "C" int doom_engine_report_shot(DoomEngine *e, DoomWeapon weapon) {
    if (!e) return -1;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->level || current_player_level(e)!=e->level) return -1;
    return doom_noise_report(e->noise,e->local_player(),e->level,weapon);
}
extern "C" int doom_engine_sample_movement(DoomEngine *e,DoomMovementSnapshot *sample) {
    if (!e || !sample) return -DOOM_SAMPLER_ARGUMENT;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->movement_sampler) return 0;
    if (!e->active || !e->in_update_scope() || current_player_level(e)!=e->level) return -DOOM_SAMPLER_ACTOR;
    float x=0,y=0,z=0;
    if (!current_player_position(e,&x,&y,&z)) return -DOOM_SAMPLER_ACTOR;
    const float position[3]={x,y,z};
    LARGE_INTEGER qpc{}; QueryPerformanceCounter(&qpc);
    void *actor=e->local_player();
    DoomEngineFrame traversal{};
    const int parkour=current_parkour(e,actor,&traversal);
    if (doom_movement_sampler_read(e->movement_sampler,actor,e->level,e->generation,e->game_thread,
            static_cast<uint64_t>(qpc.QuadPart),position,traversal.move_state,parkour,sample)) return 1;
    return -static_cast<int>(doom_movement_sampler_last_error(e->movement_sampler));
}
extern "C" void doom_engine_attack_counts(DoomEngine *e, unsigned *actors, unsigned *tested, unsigned *hit) {
    if (actors) *actors=0;
    if (tested) *tested=0;
    if (hit) *hit=0;
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (actors) *actors=doom_combat_actor_count(e->combat);
    if (tested) *tested=doom_combat_boxes_tested(e->combat);
    if (hit) *hit=doom_combat_boxes_hit(e->combat);
}
extern "C" int doom_engine_damage_observation(DoomEngine *e, DoomDamageObservation *out) {
    if (!e || !out) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    return doom_combat_last_observation(e->combat,out);
}
extern "C" unsigned doom_engine_drain_damage_observations(DoomEngine *e, DoomDamageObservation *out, unsigned capacity) {
    if (!e || !out || !capacity) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    return doom_combat_drain_observations(e->combat,out,capacity);
}
extern "C" unsigned doom_engine_drain_impacts(DoomEngine *e, DoomHitscanImpact *out, unsigned capacity) {
    if (!e || !out || !capacity) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->in_update_scope()) return 0;
    return doom_combat_drain_impacts(e->combat,out,capacity);
}
extern "C" int doom_engine_world_bind(DoomEngine *e, DoomWorldHost *host) {
    if (!e || !host) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    std::memset(host,0,sizeof(*host));
    doom_combat_world_unbind(e->combat);
    e->world_host_flags=0;
    if (!e->active || !e->combat || !e->in_update_scope() || !e->level ||
        current_player_level(e)!=e->level || !e->gameplay()) return 0;
    void *actor=e->local_player();
    if (!actor) return 0;
    if (!doom_combat_world_bind(e->combat,actor,e->level,e->generation,e->outgoing,host)) return 0;
    e->world_host_flags = (host->sweep ? 1U : 0U) | (host->damage ? 2U : 0U) |
        (host->nearby ? 4U : 0U) | (host->visible ? 8U : 0U) | (host->aim ? 16U : 0U);
    return 1;
}
extern "C" int doom_engine_lab_projectile_ready(DoomEngine *e, DoomWeapon weapon) {
#if defined(DOOM_MODE_LAB) && defined(DOOM_LAB_HOSTILITY) && defined(_DEBUG) && !defined(NDEBUG)
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->level ||
        current_player_level(e)!=e->level || !e->gameplay()) return 0;
    unsigned required=3U;
    if (weapon==DOOM_ROCKET) required|=12U;
    else if (weapon==DOOM_BFG) required|=16U;
    else if (weapon!=DOOM_PLASMA) return 0;
    return (e->world_host_flags & required)==required;
#else
    (void)e; (void)weapon;
    return 0;
#endif
}
extern "C" void doom_engine_world_unbind(DoomEngine *e) {
    if (!e) return;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    doom_combat_world_unbind(e->combat);
    e->world_host_flags=0;
}
extern "C" int doom_engine_world_sprites_open(DoomEngine *e) {
#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->level ||
        current_player_level(e)!=e->level || !e->engine_module ||
        GetModuleHandleW(L"engine_x64_rwdi.dll")!=e->engine_module) return 0;
    if (e->world_renderer)
        return e->world_renderer_level==e->level && e->world_renderer_generation==e->generation;
    e->world_renderer=doom_world_renderer_create(e->engine_module,e->level,e->generation,
                                                e->bilinear_filtering);
    if (!e->world_renderer) return 0;
    e->world_renderer_level=e->level;
    e->world_renderer_generation=e->generation;
    return 1;
#else
    (void)e; return 0;
#endif
}
extern "C" uint32_t doom_engine_world_sprite_add(DoomEngine *e, const uint8_t *rgba,
                                                 unsigned edge, DoomVec3 position, float size_metres, unsigned flags) {
#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
    if (!e || !rgba) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->world_renderer ||
        e->level!=e->world_renderer_level || e->generation!=e->world_renderer_generation ||
        current_player_level(e)!=e->level) return 0;
    const float p[3]={position.x/DOOM_WORLD_UNITS_PER_METRE,
        position.z/DOOM_WORLD_UNITS_PER_METRE,position.y/DOOM_WORLD_UNITS_PER_METRE};
    return doom_world_renderer_add(e->world_renderer,e->level,e->generation,rgba,edge,p,size_metres,flags);
#else
    (void)e; (void)rgba; (void)edge; (void)position; (void)size_metres; (void)flags; return 0;
#endif
}
extern "C" int doom_engine_world_sprite_move(DoomEngine *e, uint32_t token, DoomVec3 position) {
#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->world_renderer ||
        e->level!=e->world_renderer_level || e->generation!=e->world_renderer_generation ||
        current_player_level(e)!=e->level) return 0;
    const float p[3]={position.x/DOOM_WORLD_UNITS_PER_METRE,
        position.z/DOOM_WORLD_UNITS_PER_METRE,position.y/DOOM_WORLD_UNITS_PER_METRE};
    return doom_world_renderer_move(e->world_renderer,e->level,e->generation,token,p);
#else
    (void)e; (void)token; (void)position; return 0;
#endif
}
extern "C" int doom_engine_world_sprite_update(DoomEngine *e, uint32_t token,const uint8_t *rgba,unsigned edge) {
#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
    if (!e || !rgba || !token) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->world_renderer ||
        e->level!=e->world_renderer_level || e->generation!=e->world_renderer_generation ||
        current_player_level(e)!=e->level) return 0;
    return doom_world_renderer_update(e->world_renderer,e->level,e->generation,token,rgba,edge);
#else
    (void)e; (void)token; (void)rgba; (void)edge; return 0;
#endif
}
extern "C" int doom_engine_world_sprite_remove(DoomEngine *e, uint32_t token) {
#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->world_renderer ||
        e->level!=e->world_renderer_level || e->generation!=e->world_renderer_generation ||
        current_player_level(e)!=e->level) return 0;
    return doom_world_renderer_remove(e->world_renderer,e->level,e->generation,token);
#else
    (void)e; (void)token; return 0;
#endif
}
extern "C" int doom_engine_world_sprites_close(DoomEngine *e) {
#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
    if (!e) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->world_renderer) return 1;
    if (!e->in_update_scope() || !e->world_renderer_level ||
        current_player_level(e)!=e->world_renderer_level ||
        !doom_world_renderer_destroy(e->world_renderer,e->world_renderer_level,
                                     e->world_renderer_generation)) return 0;
    e->world_renderer=nullptr; e->world_renderer_level=nullptr; e->world_renderer_generation=0;
    return 1;
#else
    (void)e; return 1;
#endif
}
extern "C" int doom_engine_present(DoomEngine *e, const uint8_t *rgba, unsigned width, unsigned height, float scale) {
    if (!e || !rgba || width != 320 || height != 200) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->level ||
        current_player_level(e) != e->level || !e->gameplay()) return 0;
    if (!e->hud) {
        const ULONGLONG now = GetTickCount64();
        if (now < e->next_hud_attempt) return 0;
        e->next_hud_attempt = now + 5000;
        e->hud = doom_hud_host_open(e->level, e->generation);
    }
    if (e->hud) doom_hud_host_set_filtering(e->hud,e->bilinear_filtering);
    return e->hud && doom_hud_host_update(e->hud, e->level, nullptr, e->generation, rgba, scale);
}
extern "C" int doom_engine_present_layers(DoomEngine *e, const uint8_t *weapon,
                                           const uint8_t *status, const uint8_t *message,
                                           float hud_scale, float weapon_scale, float weapon_x, float weapon_y) {
    if (!e || !weapon || !status || !message) return 0;
    std::lock_guard<std::recursive_mutex> lock(e->mutex);
    if (!e->active || !e->in_update_scope() || !e->level ||
        current_player_level(e) != e->level || !e->gameplay()) return 0;
    if (!e->hud) {
        const ULONGLONG now = GetTickCount64();
        if (now < e->next_hud_attempt) return 0;
        e->next_hud_attempt = now + 5000;
        e->hud = doom_hud_host_open(e->level, e->generation);
    }
    if (e->hud) doom_hud_host_set_filtering(e->hud,e->bilinear_filtering);
    return e->hud && doom_hud_host_update_layers(e->hud, e->level, e->generation,
        weapon, status, message, hud_scale, weapon_scale, weapon_x, weapon_y);
}
