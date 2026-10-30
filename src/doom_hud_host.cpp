#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "doom_hud_host.h"
#include <windows.h>
#include <bcrypt.h>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>

/* Dynamic texture route derived from DLUMA's Workshop preview adapter. */
namespace {
void hud_trace(const char *message) {
#ifdef DOOM_MODE_LAB
    OutputDebugStringA(message);
#else
    (void)message;
#endif
}
struct TtlString { const char *data; uint32_t size, capacity; };
TtlString str(const char *s) { TtlString r = {s, (uint32_t)strlen(s), (uint32_t)strlen(s)}; return r; }
using Create = void *(__fastcall *)(void *, const TtlString *, int, int, unsigned, unsigned char, unsigned);
using Update = void (__fastcall *)(void *, void *, const void *, int, unsigned, int, unsigned, unsigned, unsigned, unsigned, unsigned);
using Unload = unsigned char (__fastcall *)(void *, void *);
using LoadUi = void (__fastcall *)(void *, const TtlString *, const TtlString *, int, int, int, int, int, int, int, int, unsigned char, TtlString *);
using UnloadUi = int (__fastcall *)(void *, const TtlString *);
using IsLoaded = unsigned char (__fastcall *)(void *, const TtlString *);
using SetTexture = unsigned char (__fastcall *)(void *, const TtlString *);
using Register = int (__fastcall *)(void *, const TtlString *, unsigned, int, unsigned char, const TtlString *, unsigned);
using Screen = void *(__fastcall *)(void *, int);
using FindElement = void *(__fastcall *)(void *, const TtlString *, unsigned char);
using Show = void (__fastcall *)(void *, int, void *);
using Hide = void (__fastcall *)(void *, int);
using Unregister = unsigned char (__fastcall *)(void *, int);
using Lookup = int (__fastcall *)(void *, const TtlString *);
using IsVisible = unsigned char (__fastcall *)(void *);
using SetVisible = void (__fastcall *)(void *, unsigned char);
using GetOwner = void *(__fastcall *)(void *);
using GetScreenId = int (__fastcall *)(void *);
using GetScriptName = const TtlString *(__fastcall *)(void *);
struct Vec3 { float x, y, z; };
using SetPos = void (__fastcall *)(void *, const Vec3 *);
using SetSize = void (__fastcall *)(void *, float);
using AddSource = unsigned char (__cdecl *)(const char *, unsigned);
using FindSource = void *(__cdecl *)(const char *);
bool identity(HMODULE module, const unsigned char *expected) {
    wchar_t path[32768]; unsigned char digest[32], data[65536]; DWORD n, got = 0;
    BCRYPT_ALG_HANDLE alg = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    n = GetModuleFileNameW(module, path, 32768); if (!n || n >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    bool ok = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    while (ok) { if (!ReadFile(file, data, sizeof(data), &got, nullptr)) { ok = false; break; } if (!got) break; ok = BCryptHashData(hash, data, got, 0) >= 0; }
    if (ok) ok = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash); if (alg) BCryptCloseAlgorithmProvider(alg, 0); CloseHandle(file);
    return ok && !memcmp(digest, expected, sizeof(digest));
}
static const unsigned char engine_hash[32] = {0xa1,0x8c,0xa1,0x10,0x36,0x14,0x94,0x7a,0xe7,0xff,0x95,0x6c,0x68,0x52,0xf4,0x51,0x23,0xb4,0x40,0x56,0x3a,0x96,0x7d,0x9e,0x1e,0x49,0x6c,0x42,0xfa,0x57,0x4a,0x3c};
static const unsigned char tools_engine_hash[32] = {0x5b,0x80,0x90,0xbf,0xc3,0xd3,0xa2,0x5a,0x24,0xda,0x18,0x46,0xa3,0xaa,0xfd,0x65,0xc9,0x7e,0x0d,0x1e,0x70,0x0b,0x5d,0xfe,0xbf,0xc7,0x13,0x27,0x37,0xb6,0xbf,0xf6};
static const unsigned char fs_hash[32] = {0xcb,0x6f,0xf4,0xb0,0xc9,0xaa,0x62,0xc8,0xa3,0x57,0x42,0xd4,0x63,0xae,0x3c,0x34,0xfe,0x80,0x37,0x67,0xe8,0xe2,0xac,0x9b,0x33,0xe4,0x60,0x48,0x86,0xb9,0x84,0x6d};
static const unsigned char tools_fs_hash[32] = {0xc8,0x33,0xeb,0xd6,0x35,0x57,0x2e,0xc9,0xd8,0xd7,0x73,0x47,0x9f,0xa0,0xdd,0x6e,0x4f,0xa8,0x0d,0x9f,0x95,0x8a,0x08,0x76,0xc8,0xf0,0x7e,0x5f,0x11,0x89,0xb6,0x5a};
static const unsigned char game_hash[32] = {0xcb,0xc7,0xdd,0x79,0x23,0x77,0x89,0x36,0x34,0x1c,0xdf,0xb7,0x55,0x47,0x4a,0x0c,0x71,0x40,0x79,0xd9,0x10,0x84,0x9d,0xf1,0x6f,0xfc,0x5e,0xc8,0x8a,0x10,0x3b,0x95};
static const unsigned char tools_game_hash[32] = {0x8e,0x19,0xc8,0x9c,0x31,0x6e,0x31,0x9e,0x1e,0x88,0xd0,0xc7,0xa6,0xf6,0xa9,0x35,0xc4,0xb9,0xc3,0x81,0x90,0x5c,0xab,0x48,0x44,0x9c,0xee,0x8f,0xd6,0xcc,0xba,0x54};
bool known_engine(HMODULE module) { return identity(module, engine_hash) || identity(module, tools_engine_hash); }
HANDLE mounted_pack = INVALID_HANDLE_VALUE;
bool mount_pack() {
    static const unsigned char add_prefix[] = {0x48,0x89,0x7c,0x24,0x20,0x55,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d};
    HMODULE fs = GetModuleHandleW(L"filesystem_x64_rwdi.dll"), self = nullptr;
    if (mounted_pack != INVALID_HANDLE_VALUE) return true;
    if (!fs || (!identity(fs, fs_hash) && !identity(fs, tools_fs_hash)) || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&mount_pack), &self)) return false;
    auto *base = reinterpret_cast<unsigned char *>(fs);
    auto add = reinterpret_cast<AddSource>(GetProcAddress(fs, "?add_source@fs@@YA_NPEBDW4ENUM@FFSAddSourceFlags@@@Z"));
    auto find = reinterpret_cast<FindSource>(GetProcAddress(fs, "?find_source@fs@@YAPEAVCFSSource@@PEBD@Z"));
    if (reinterpret_cast<void *>(add) != base + 0x12dd0 || reinterpret_cast<void *>(find) != base + 0x14430 || memcmp(base + 0x12dd0, add_prefix, sizeof(add_prefix))) return false;
    wchar_t path[32768]; DWORD n = GetModuleFileNameW(self, path, 32768);
    if (!n || n >= 32768) return false;
    while (n && path[n-1] != L'\\' && path[n-1] != L'/') --n;
    if (!n || n + 13 >= 32768) return false;
    memcpy(path + n, L"DoomMode.pak", sizeof(L"DoomMode.pak"));
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    char utf8[8192]; int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
    if (!count) { CloseHandle(file); return false; }
    bool success = false;
    __try { success = find(utf8) != nullptr || (add(utf8, 9u) && find(utf8)); } __except(EXCEPTION_EXECUTE_HANDLER) { success = false; }
    if (!success) { CloseHandle(file); return false; }
    mounted_pack = file; return true;
}
}
struct HudTexture {
    void *texture; uint8_t *pixels, *last_frame;
    unsigned width, height, backing_width, backing_height, viewport_width, viewport_height, source_height;
    int origin_x, origin_y; float scale; bool registered, ready, has_frame;
    char actual_name[128], alias[64];
};
struct DoomHudHost {
    void *level, *image; uint32_t generation; bool bound;
    HudTexture current, pending, retiring; ULONGLONG next_texture_attempt;
    Create create; Update update; Unload unload; LoadUi load_ui; UnloadUi unload_ui;
    IsLoaded loaded; SetTexture set_texture;
    int screen_id; Show show; Hide hide; Unregister unregister; bool owns_screen, shown;
    SetPos position; SetSize width, height; float scale; LONG client_width, client_height;
    Screen find_screen; FindElement find_element; GetOwner get_owner;
    GetScreenId get_screen_id; GetScriptName get_script_name; void *root;
    Lookup lookup; IsVisible is_visible; SetVisible set_visible;
    void *stock_root; void *stock_elements[8]; bool stock_owned[8];
    void *death_root, *death_elements[5]; bool death_owned[5];
    DoomHudHost *layers[2]; unsigned layer_kind; bool split_layers, inside_layered_update;
};
static const char *const stock_ids[8] = {"HudHealthIndicator", "HudStamina", "HudRadar", "HudPrimaryWeaponIndicator", "HudSecondaryWeaponIndicator", "HudWeaponSelection", "HudCrosshair", "HudReloadNotify"};
static void *find_stock_root(DoomHudHost *h) {
    TtlString hud = str("HUD.xui"); int id = h->lookup(h->level, &hud);
    if (id < 0) return nullptr;
    void *root = h->find_screen(h->level, id);
    if (!root) return nullptr;
    void *owner = h->get_owner(root);
    if (!owner || h->get_screen_id(owner) != id) return nullptr;
    const TtlString *script = h->get_script_name(owner);
    if (!script || !script->data || !script->size || script->size >= 512) return nullptr;
    char name[512] = {}; memcpy(name, script->data, script->size);
    const char *base = name; for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    return (!_stricmp(base, "HUD") || !_stricmp(base, "HUD.xui")) ? root : nullptr;
}
static void stock_visibility(DoomHudHost *h, bool suppress) {
    void *root = find_stock_root(h);
    if (root != h->stock_root) {
        memset(h->stock_elements, 0, sizeof(h->stock_elements));
        memset(h->stock_owned, 0, sizeof(h->stock_owned));
        h->stock_root = root;
    }
    if (!root) return;
    for (unsigned i = 0; i < 8; ++i) {
        TtlString id = str(stock_ids[i]); void *element = h->find_element(root, &id, 1);
        if (element != h->stock_elements[i]) { h->stock_elements[i] = element; h->stock_owned[i] = false; }
        if (!element) continue;
        if (suppress) {
            if (h->is_visible(element)) { h->set_visible(element, 0); h->stock_owned[i] = true; }
        } else if (h->stock_owned[i]) {
            if (!h->is_visible(element)) h->set_visible(element, 1);
            h->stock_owned[i] = false;
        }
    }
}
static void death_visibility(DoomHudHost *h, bool suppress) {
    static const char *const ids[5] = {"T_Dead", "T_RespawnInfo", "T_DeathPenalty", "T_Hint", "I_HintImage"};
    void *root=find_stock_root(h);
    TtlString group_id=str("HudFadeCountdown");
    void *group=root ? h->find_element(root,&group_id,1) : nullptr;
    if(group!=h->death_root) {
        h->death_root=group; std::memset(h->death_elements,0,sizeof(h->death_elements));
        std::memset(h->death_owned,0,sizeof(h->death_owned));
    }
    if(!group) return;
    for(unsigned i=0;i<5;i++) {
        TtlString id=str(ids[i]); void *element=h->find_element(group,&id,1);
        if(element!=h->death_elements[i]) { h->death_elements[i]=element; h->death_owned[i]=false; }
        if(!element) continue;
        if(suppress) { if(h->is_visible(element)) { h->set_visible(element,0); h->death_owned[i]=true; } }
        else if(h->death_owned[i]) { if(!h->is_visible(element)) h->set_visible(element,1); h->death_owned[i]=false; }
    }
}
static bool refresh_screen(DoomHudHost *h) {
    if (!h->owns_screen || !h->find_screen || !h->find_element || !h->get_owner ||
        !h->get_screen_id || !h->get_script_name) return false;
    void *root = h->find_screen(h->level, h->screen_id);
    if (!root) { h->root = nullptr; h->image = nullptr; h->bound = false; h->shown = false; return false; }
    void *owner = h->get_owner(root);
    if (!owner || h->get_screen_id(owner) != h->screen_id) { h->root = nullptr; h->image = nullptr; h->bound = false; h->shown = false; return false; }
    const TtlString *script = h->get_script_name(owner);
    if (!script || !script->data || !script->size || script->size >= 512) { h->root = nullptr; h->image = nullptr; h->bound = false; h->shown = false; return false; }
    char name[512] = {}; memcpy(name, script->data, script->size);
    const char *base = name;
    for (const char *p = name; *p; ++p) if (*p == '/' || *p == '\\') base = p + 1;
    if (_stricmp(base, "DLUMA_ModHud") && _stricmp(base, "DLUMA_ModHud.xui")) { h->root = nullptr; h->image = nullptr; h->bound = false; h->shown = false; return false; }
    TtlString id = str("I_DLUMAModHud");
    void *image = h->find_element(root, &id, 1);
    if (!image) { h->root = root; h->image = nullptr; h->bound = false; return false; }
    if (h->root != root || h->image != image) { h->root = root; h->image = image; h->bound = false; h->shown = false; h->scale = 0; }
    static const char *const ids[2] = {"I_DLUMAStatusBar", "I_DLUMAMessage"};
    for (unsigned i = 0; i < 2; ++i) if (h->layers[i]) {
        TtlString layer_id = str(ids[i]);
        void *child = h->find_element(root, &layer_id, 1);
        if (!child || h->get_owner(child) != owner) return false;
        if (h->layers[i]->image != child) {
            h->layers[i]->image = child; h->layers[i]->bound = false;
            h->layers[i]->shown = false; h->layers[i]->scale = 0;
        }
    }
    return true;
}
static bool release_texture(DoomHudHost *h, HudTexture *slot, bool level_valid) {
    if (slot->registered) {
        if (!level_valid) return false;
        TtlString alias = str(slot->alias);
        /* UnloadUITexture returns the number of outstanding UI references.
         * Keep the texture alive until the image releases its old alias. */
        if (h->unload_ui(h->level, &alias) != 0) return false;
        slot->registered = false;
    }
    if (slot->texture) (void)h->unload(nullptr, slot->texture);
    free(slot->pixels); free(slot->last_frame);
    std::memset(slot, 0, sizeof(*slot));
    return true;
}
static bool physical_texture_fits(unsigned width, unsigned height) {
    if (!width || !height || width > 8192 || height > 8192) return false;
    unsigned backing_width = 1, backing_height = 1;
    while (backing_width < width) backing_width *= 2;
    while (backing_height < height) backing_height *= 2;
    return static_cast<size_t>(backing_width) * backing_height <= (64u * 1024u * 1024u) / 4;
}
static bool create_texture(DoomHudHost *h, HudTexture *slot, unsigned width, unsigned height, unsigned source_height) {
    static volatile LONG serial = 0;
    constexpr size_t MaxBytes = 64u * 1024u * 1024u;
    if (!width || !height || width > 8192 || height > 8192 ||
        static_cast<size_t>(width) * height > MaxBytes / 4) return false;
    unsigned backing_width = 1, backing_height = 1;
    while (backing_width < width) backing_width *= 2;
    while (backing_height < height) backing_height *= 2;
    if (static_cast<size_t>(backing_width) * backing_height > MaxBytes / 4) return false;
    const size_t bytes = static_cast<size_t>(backing_width) * backing_height * 4;
    slot->pixels = static_cast<uint8_t *>(calloc(bytes, 1));
    slot->last_frame = static_cast<uint8_t *>(malloc(320u * source_height * 4u));
    if (!slot->pixels || !slot->last_frame) { release_texture(h, slot, true); return false; }
    const unsigned sequence = static_cast<unsigned>(InterlockedIncrement(&serial));
    char name_buffer[64];
    std::snprintf(name_buffer, sizeof(name_buffer), "dluma_doom_hud_%u", sequence);
    std::snprintf(slot->alias, sizeof(slot->alias), "dluma_doom_hud_ui_%u", sequence);
    TtlString name = str(name_buffer);
    slot->texture = h->create(nullptr, &name, static_cast<int>(backing_width), static_cast<int>(backing_height), 1, 0, 2);
    if (!slot->texture) { release_texture(h, slot, true); return false; }
    const char *actual = *reinterpret_cast<const char **>(static_cast<unsigned char *>(slot->texture) + 0x60);
    size_t i = 0;
    if (actual) for (; i + 1 < sizeof(slot->actual_name) && actual[i]; ++i) slot->actual_name[i] = actual[i];
    if (!i || i + 1 == sizeof(slot->actual_name) || std::strncmp(slot->actual_name, name_buffer, std::strlen(name_buffer))) {
        release_texture(h, slot, true); return false;
    }
    slot->width = width; slot->height = height; slot->source_height = source_height;
    slot->backing_width = backing_width; slot->backing_height = backing_height;
    return true;
}
/* Expand to one texel per output pixel. A 1:1 image avoids sampling between
 * adjacent Doom pixels even when the game's UI sampler is linear. */
static void expand_frame(HudTexture *slot, const uint8_t *rgba, float scale,
                         unsigned viewport_width, unsigned viewport_height, int origin_x, int origin_y,
                         int canvas_left, int canvas_top, int canvas_width, int canvas_height,
                         bool physical_size) {
    const int width = static_cast<int>(slot->width), height = static_cast<int>(slot->height);
    const int stride = static_cast<int>(slot->backing_width);
    if (!physical_size) {
        for (unsigned y = 0; y < slot->source_height; ++y)
            std::memcpy(slot->pixels + static_cast<size_t>(y) * stride * 4,
                        rgba + static_cast<size_t>(y) * 320 * 4, 320u * 4u);
        std::memcpy(slot->last_frame, rgba, 320u * slot->source_height * 4u);
        slot->scale = scale; slot->has_frame = true;
        slot->viewport_width = viewport_width; slot->viewport_height = viewport_height;
        slot->origin_x = origin_x; slot->origin_y = origin_y;
        return;
    }
    const int left = canvas_left - origin_x, top = canvas_top - origin_y;
    const int clip_left = left < 0 ? 0 : left;
    const int clip_right = left + canvas_width > width ? width : left + canvas_width;
    int previous_source_y = -1;
    for (int y = 0; y < height; ++y) {
        uint8_t *row = slot->pixels + static_cast<size_t>(y) * stride * 4;
        if (y < top || y >= top + canvas_height || clip_left >= clip_right) {
            std::memset(row, 0, static_cast<size_t>(width) * 4); previous_source_y = -1; continue;
        }
        const int source_y = static_cast<int>((static_cast<int64_t>(y - top) * slot->source_height) / canvas_height);
        if (source_y == previous_source_y) {
            std::memcpy(row, row - static_cast<size_t>(stride) * 4, static_cast<size_t>(width) * 4);
            continue;
        }
        previous_source_y = source_y;
        std::memset(row, 0, static_cast<size_t>(width) * 4);
        auto *out = reinterpret_cast<uint32_t *>(row);
        for (int x = 0; x < 320; ++x) {
            int begin = left + static_cast<int>((static_cast<int64_t>(x) * canvas_width + 319) / 320);
            int end = left + static_cast<int>((static_cast<int64_t>(x + 1) * canvas_width + 319) / 320);
            if (begin < clip_left) begin = clip_left;
            if (end > clip_right) end = clip_right;
            if (begin >= end) continue;
            uint32_t pixel;
            std::memcpy(&pixel, rgba + (static_cast<size_t>(source_y) * 320 + x) * 4, 4);
            for (int target = begin; target < end; ++target) out[target] = pixel;
        }
    }
    std::memcpy(slot->last_frame, rgba, 320u * slot->source_height * 4u);
    slot->scale = scale; slot->has_frame = true;
    slot->viewport_width = viewport_width; slot->viewport_height = viewport_height;
    slot->origin_x = origin_x; slot->origin_y = origin_y;
}
extern "C" DoomHudHost *doom_hud_host_open(void *level, uint32_t generation) {
    HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    HMODULE game = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
    if (!engine || !level || !generation || !known_engine(engine)) { hud_trace("Doom HUD: engine or level unavailable\n"); return nullptr; }
    if (!game || (!identity(game, game_hash) && !identity(game, tools_game_hash))) { hud_trace("Doom HUD: game profile unavailable\n"); return nullptr; }
    __try {
        unsigned char *base = reinterpret_cast<unsigned char *>(game);
        void **table = *reinterpret_cast<void ***>(level);
        static const unsigned char pack_zero[] = {0x33,0xc0,0xc3};
        if (table != reinterpret_cast<void **>(base + 0x1581c98) || table[133] != base + 0x1481a0 || memcmp(base + 0x1481a0, pack_zero, sizeof(pack_zero))) return nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    if (!mount_pack()) { hud_trace("Doom HUD: companion mount failed\n"); return nullptr; }
    auto reg = reinterpret_cast<Register>(GetProcAddress(engine, "?RegisterUIScreen@ILevel@@QEAAHAEBV?$string_base@D@ttl@@HH_N0H@Z"));
    auto load = reinterpret_cast<Screen>(GetProcAddress(engine, "?LoadUIScreen@ILevel@@QEAAPEAVIUIScreen@@H@Z"));
    auto find_screen = reinterpret_cast<Screen>(GetProcAddress(engine, "?FindUiScreen@ILevel@@QEBAPEAVIUIElement@@H@Z"));
    auto find_element = reinterpret_cast<FindElement>(GetProcAddress(engine, "?FindElement@IUIElement@@QEAAPEAV1@AEBV?$string_base@D@ttl@@_N@Z"));
    auto show = reinterpret_cast<Show>(GetProcAddress(engine, "?ShowUIScreen@ILevel@@QEAAXHPEAVIUIData@@@Z"));
    auto hide = reinterpret_cast<Hide>(GetProcAddress(engine, "?HideUIScreen@ILevel@@QEAAXH@Z"));
    auto unregister = reinterpret_cast<Unregister>(GetProcAddress(engine, "?UnRegisterUIScreen@ILevel@@QEAA_NH@Z"));
    auto get_owner = reinterpret_cast<GetOwner>(GetProcAddress(engine, "?GetOwnerScreen@IUIElement@@QEBAPEAVIUIScreen@@XZ"));
    auto get_id = reinterpret_cast<GetScreenId>(GetProcAddress(engine, "?GetScreenId@IUIScreen@@QEBAHXZ"));
    auto get_script = reinterpret_cast<GetScriptName>(GetProcAddress(engine, "?GetScriptName@IUIScreen@@QEAAAEBV?$string_base@D@ttl@@XZ"));
    auto lookup = reinterpret_cast<Lookup>(GetProcAddress(engine, "?GetUIScreenIdByScriptName@ILevel@@QEAAHAEBV?$string_base@D@ttl@@@Z"));
    auto is_visible = reinterpret_cast<IsVisible>(GetProcAddress(engine, "?IsVisible@IUIElement@@QEBA_NXZ"));
    auto set_visible = reinterpret_cast<SetVisible>(GetProcAddress(engine, "?SetVisible@IUIElement@@QEAAX_N@Z"));
    if (!reg || !load || !find_screen || !find_element || !show || !hide || !unregister || !get_owner || !get_id || !get_script || !lookup || !is_visible || !set_visible) { hud_trace("Doom HUD: screen exports unavailable\n"); return nullptr; }
    int screen_id = -1; void *image = nullptr; DoomHudHost *host = nullptr;
    __try {
        TtlString path = str("DLUMA_ModHud.xui"), empty = str("");
        screen_id = reg(level, &path, 0, 0, 0, &empty, 0xffffffffu);
        if (screen_id < 0) hud_trace("Doom HUD: screen registration rejected\n");
        if (screen_id >= 0) {
            void *loaded = load(level, screen_id);
            if (!loaded) hud_trace("Doom HUD: LoadUIScreen returned null\n");
            if (loaded) {
                void *root = find_screen(level, screen_id); TtlString id = str("I_DLUMAModHud");
                if (!root) hud_trace("Doom HUD: loaded screen root unavailable\n");
                if (root) { image = find_element(root, &id, 1); if (!image) hud_trace("Doom HUD: image element lookup failed\n"); }
            }
        }
        if (!image && screen_id >= 0) hud_trace("Doom HUD: screen or image unavailable\n");
        if (image) host = doom_hud_host_create(level, image, generation);
        if (!host && screen_id >= 0) (void)unregister(level, screen_id);
        if (host) {
            host->screen_id = screen_id; host->show = show; host->hide = hide; host->unregister = unregister; host->owns_screen = true;
            host->find_screen = find_screen; host->find_element = find_element; host->get_owner = get_owner;
            host->get_screen_id = get_id; host->get_script_name = get_script;
            host->lookup = lookup; host->is_visible = is_visible; host->set_visible = set_visible;
            if (!refresh_screen(host)) { hud_trace("Doom HUD: screen identity check failed\n"); (void)unregister(level, screen_id); host->owns_screen = false; doom_hud_host_destroy(host, level, generation); host = nullptr; }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { hud_trace("Doom HUD: exception during screen initialization\n"); return nullptr; }
    if (host) hud_trace("Doom HUD: screen initialized\n");
    return host;
}
extern "C" DoomHudHost *doom_hud_host_create(void *level, void *image, uint32_t generation) {
    static const unsigned char create_prefix[] = {0x48,0x83,0xec,0x68,0x48,0x8b,0x02,0x48,0x8d,0x15,0x73,0xb1,0x5c,0x00};
    static const unsigned char update_prefix[] = {0x48,0x83,0xec,0x68,0x8b,0x84,0x24,0xc0,0x00,0x00,0x00};
    HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!engine || !level || !image || !generation || !known_engine(engine)) return nullptr;
    auto *base = reinterpret_cast<unsigned char *>(engine);
    if (memcmp(base + 0x29c1c0, create_prefix, sizeof(create_prefix)) || memcmp(base + 0x29c230, update_prefix, sizeof(update_prefix))) return nullptr;
    auto *h = static_cast<DoomHudHost *>(calloc(1, sizeof(DoomHudHost))); if (!h) return nullptr;
    h->level = level; h->image = image; h->generation = generation;
    h->create = reinterpret_cast<Create>(base + 0x29c1c0); h->update = reinterpret_cast<Update>(base + 0x29c230);
    h->unload = reinterpret_cast<Unload>(base + 0x29a9f0);
    h->load_ui = reinterpret_cast<LoadUi>(GetProcAddress(engine, "?LoadUITexture@ILevel@@QEAAXAEBV?$string_base@D@ttl@@0HHHHHHHH_NV23@@Z"));
    h->unload_ui = reinterpret_cast<UnloadUi>(GetProcAddress(engine, "?UnloadUITexture@ILevel@@QEAAHAEBV?$string_base@D@ttl@@@Z"));
    h->loaded = reinterpret_cast<IsLoaded>(GetProcAddress(engine, "?IsUITextureLoaded@ILevel@@QEAA_NAEBV?$string_base@D@ttl@@@Z"));
    h->set_texture = reinterpret_cast<SetTexture>(GetProcAddress(engine, "?SetTexture@IUIImage@@QEAA_NAEBV?$string_base@D@ttl@@@Z"));
    h->position = reinterpret_cast<SetPos>(GetProcAddress(engine, "?SetRelativePos@IUIElement@@QEAAXAEBVvec3@@@Z"));
    h->width = reinterpret_cast<SetSize>(GetProcAddress(engine, "?SetSizeX@IUIElement@@QEAAXM@Z"));
    h->height = reinterpret_cast<SetSize>(GetProcAddress(engine, "?SetSizeY@IUIElement@@QEAAXM@Z"));
    h->set_visible = reinterpret_cast<SetVisible>(GetProcAddress(engine, "?SetVisible@IUIElement@@QEAAX_N@Z"));
    if (!h->load_ui || !h->unload_ui || !h->loaded || !h->set_texture || !h->position || !h->width || !h->height || !h->set_visible) { free(h); return nullptr; }
    return h;
}
extern "C" int doom_hud_host_update(DoomHudHost *h, void *level, void *image, uint32_t generation, const uint8_t *rgba, float scale) {
    if (!h || !rgba || level != h->level || generation != h->generation) return 0;
    __try {
        if (h->owns_screen && !refresh_screen(h)) return 0;
        if (!h->image || (image && image != h->image)) return 0;
        if (h->owns_screen && h->split_layers && !h->inside_layered_update) {
            for (unsigned i = 0; i < 2; ++i) if (h->layers[i] && h->layers[i]->image) {
                h->set_visible(h->layers[i]->image, 0); h->layers[i]->shown = false;
            }
            h->split_layers = false;
        }
        image = h->image;
        HWND window = GetForegroundWindow(); DWORD pid = 0; RECT rect;
        if (window) window = GetAncestor(window, GA_ROOT);
        if (!window || !GetWindowThreadProcessId(window, &pid) || pid != GetCurrentProcessId() ||
            !GetClientRect(window, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) return 0;
        if (!(scale >= 0.5f && scale <= 2.0f)) return 0;
        const LONG client_width = rect.right - rect.left, client_height = rect.bottom - rect.top;
        const unsigned source_height = h->layer_kind == 1 ? 32u : h->layer_kind == 2 ? 16u : 200u;
        const int full_height = static_cast<int>(client_height * scale + 0.5f);
        const int canvas_width = (full_height * 4 + 1) / 3;
        const int canvas_height = (full_height * static_cast<int>(source_height) + 100) / 200;
        const int canvas_left = h->layer_kind == 2 ? 0 : (client_width - canvas_width) / 2;
        const int canvas_top = h->layer_kind == 2 ? 0 : client_height - canvas_height;
        int origin_x = canvas_left < 0 ? 0 : canvas_left;
        int origin_y = canvas_top < 0 ? 0 : canvas_top;
        const int right = canvas_left + canvas_width > client_width ? client_width : canvas_left + canvas_width;
        const int bottom = canvas_top + canvas_height > client_height ? client_height : canvas_top + canvas_height;
        if (right <= origin_x || bottom <= origin_y) return 0;
        const unsigned visible_width = static_cast<unsigned>(right - origin_x), visible_height = static_cast<unsigned>(bottom - origin_y);
        const bool physical_size = physical_texture_fits(visible_width, visible_height);
        const unsigned draw_width = physical_size ? visible_width : 320u, draw_height = physical_size ? visible_height : source_height;
        if (!physical_size) { origin_x = canvas_left; origin_y = canvas_top; }
        if (h->retiring.texture) (void)release_texture(h, &h->retiring, true);
        HudTexture *slot = &h->current;
        if (!slot->texture || slot->width != draw_width || slot->height != draw_height || slot->source_height != source_height) {
            slot = &h->pending;
            if (slot->texture && (slot->width != draw_width || slot->height != draw_height || slot->source_height != source_height) &&
                !release_texture(h, slot, true)) return h->bound ? 1 : 0;
            if (!slot->texture) {
                if (GetTickCount64() < h->next_texture_attempt) return h->bound ? 1 : 0;
                if (!create_texture(h, slot, draw_width, draw_height, source_height)) {
                    h->next_texture_attempt = GetTickCount64() + 5000;
                    return h->bound ? 1 : 0;
                }
                h->next_texture_attempt = 0;
            }
        }
        if (!slot->has_frame || slot->scale != scale ||
            slot->viewport_width != static_cast<unsigned>(client_width) ||
            slot->viewport_height != static_cast<unsigned>(client_height) ||
            slot->origin_x != origin_x || slot->origin_y != origin_y ||
            std::memcmp(slot->last_frame, rgba, 320u * source_height * 4u)) {
            expand_frame(slot, rgba, scale, static_cast<unsigned>(client_width),
                         static_cast<unsigned>(client_height), origin_x, origin_y,
                         canvas_left, canvas_top, canvas_width, canvas_height, physical_size);
            h->update(nullptr, slot->texture, slot->pixels, 0, 0, 0, 0, slot->backing_width,
                      slot->backing_height, slot->backing_width * 4, 32);
        }
        TtlString alias = str(slot->alias), actual = str(slot->actual_name), material = {};
        if (slot->registered && slot->ready && !h->loaded(level, &alias)) {
            slot->registered = false; slot->ready = false;
            if (slot == &h->current) h->bound = false;
        }
        if (!slot->registered) {
            h->load_ui(level, &alias, &actual, static_cast<int>(slot->backing_width),
                       static_cast<int>(slot->backing_height), 0, 0,
                       static_cast<int>(slot->width), static_cast<int>(slot->height), 0, 0, 0, &material);
            slot->registered = true;
        }
        if (!h->loaded(level, &alias)) return h->bound ? 1 : 0;
        slot->ready = true;
        if (slot == &h->pending) {
            if (h->retiring.texture && !release_texture(h, &h->retiring, true))
                return h->bound ? 1 : 0;
            if (!h->set_texture(image, &alias)) return h->bound ? 1 : 0;
            h->retiring = h->current;
            h->current = h->pending; std::memset(&h->pending, 0, sizeof(h->pending));
            if (h->retiring.texture) (void)release_texture(h, &h->retiring, true);
            h->bound = true;
            hud_trace("Doom HUD: texture bound\n");
        } else if (!h->bound) {
            h->bound = h->set_texture(image, &alias) != 0;
        }
        if (h->bound && (scale != h->scale || client_width != h->client_width || client_height != h->client_height)) {
            Vec3 position = {static_cast<float>(origin_x), static_cast<float>(origin_y), 0.0f};
            h->width(image, static_cast<float>(physical_size ? draw_width : canvas_width));
            h->height(image, static_cast<float>(physical_size ? draw_height : canvas_height));
            h->position(image, &position);
            h->scale = scale; h->client_width = client_width; h->client_height = client_height;
        }
        if (h->bound && !h->shown) { h->set_visible(image, 1); if (h->owns_screen) h->show(level, h->screen_id, nullptr); h->shown = true; }
        if (h->owns_screen && h->bound && h->shown && (!h->split_layers || (h->layers[0] && h->layers[0]->bound))) stock_visibility(h, true);
        return h->bound ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" int doom_hud_host_update_layers(DoomHudHost *h, void *level, uint32_t generation,
                                             const uint8_t *weapon, const uint8_t *status,
                                             const uint8_t *message, float hud_scale, float weapon_scale) {
    if (!h || !h->owns_screen || !weapon || !status || !message ||
        level != h->level || generation != h->generation) return 0;
    __try {
        if (!refresh_screen(h)) return 0;
        void *owner = h->get_owner(h->root);
        static const char *const ids[2] = {"I_DLUMAStatusBar", "I_DLUMAMessage"};
        h->split_layers = true;
        for (unsigned i = 0; i < 2; ++i) if (!h->layers[i]) {
            TtlString id = str(ids[i]);
            void *image = h->find_element(h->root, &id, 1);
            if (!image || !owner || h->get_owner(image) != owner) return 0;
            h->layers[i] = doom_hud_host_create(level, image, generation);
            if (!h->layers[i]) return 0;
            h->layers[i]->layer_kind = i + 1;
        }
        h->inside_layered_update = true;
        const int weapon_ready = doom_hud_host_update(h, level, nullptr, generation, weapon, weapon_scale);
        h->inside_layered_update = false;
        const int status_ready = doom_hud_host_update(h->layers[0], level, nullptr, generation, status, hud_scale);
        const int message_ready = doom_hud_host_update(h->layers[1], level, nullptr, generation, message, 1.0f);
        stock_visibility(h, weapon_ready && status_ready && h->shown);
        return weapon_ready && status_ready && message_ready;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
extern "C" void doom_hud_host_destroy(DoomHudHost *h, void *level, uint32_t generation) {
    if (!h) return;
    __try {
        if (level == h->level && generation == h->generation) {
            if (h->owns_screen) death_visibility(h, false);
            if (h->owns_screen) stock_visibility(h, false);
            if (h->owns_screen && refresh_screen(h)) { if (h->shown) h->hide(level, h->screen_id); (void)h->unregister(level, h->screen_id); }
            for (unsigned i = 0; i < 2; ++i) if (h->layers[i]) {
                doom_hud_host_destroy(h->layers[i], level, generation); h->layers[i] = nullptr;
            }
            release_texture(h, &h->pending, true);
            release_texture(h, &h->current, true);
            release_texture(h, &h->retiring, true);
        } else {
            for (unsigned i = 0; i < 2; ++i) if (h->layers[i]) {
                doom_hud_host_destroy(h->layers[i], level, generation); h->layers[i] = nullptr;
            }
            release_texture(h, &h->pending, false);
            release_texture(h, &h->current, false);
            release_texture(h, &h->retiring, false);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    free(h);
}
extern "C" void doom_hud_host_hide(DoomHudHost *h, void *level, uint32_t generation) {
    if (!h || level != h->level || generation != h->generation || !h->shown || !h->owns_screen) return;
    __try { stock_visibility(h, false); if (refresh_screen(h)) h->hide(level, h->screen_id); h->shown = false; } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
extern "C" void doom_hud_host_death_screen(DoomHudHost *h, void *level, uint32_t generation, int suppress) {
    if (!h || !h->owns_screen || level!=h->level || generation!=h->generation) return;
    __try { death_visibility(h, suppress!=0); } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
