#include "doom_world_display.h"
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
constexpr unsigned MaxSprites = DOOM_WORLD_MAX_PROJECTILES + DOOM_WORLD_MAX_PICKUPS;
constexpr unsigned MaxImages = 32, MaxNativeSprites = 64;
struct Image {
    char name[9]{};
    uint8_t *pixels = nullptr;
    unsigned edge = 0, height = 0;
};
struct Sprite {
    uint32_t token = 0;
    unsigned age = 0, edge = 0;
    char name[9]{};
};
struct Wanted {
    const char *name = nullptr;
    DoomVec3 position{};
    unsigned age = 0, edge = 0;
    bool pickup = false;
};
unsigned u16(const uint8_t *p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); }
Wanted desired(const DoomWorld *world, unsigned index) {
    Wanted w{};
    if (index < DOOM_WORLD_MAX_PROJECTILES) {
        const DoomProjectile &p = world->projectiles[index];
        w.name = doom_world_projectile_sprite(&p); w.position = p.position; w.age = p.age;
        w.edge = p.weapon == DOOM_BFG ? 256u : p.weapon == DOOM_ROCKET ? 128u : 64u;
    } else {
        const DoomPickup &p = world->pickups[index - DOOM_WORLD_MAX_PROJECTILES];
        if (p.active) w.name = doom_world_pickup_sprite(p.kind);
        w.position = p.position; w.age = p.age; w.edge = 32; w.pickup = true;
    }
    return w;
}
}
struct DoomWorldDisplay {
    std::array<Sprite, MaxSprites> sprites{};
    std::array<Image, MaxImages> images{};
    uint32_t generation = 0;
    bool open = false, blocked = false;
};

extern "C" DoomWorldDisplay *doom_world_display_create(void) {
    return new (std::nothrow) DoomWorldDisplay;
}
static const Image *picture(DoomWorldDisplay *d, const DoomWad *wad, const Wanted &w) {
    for (const Image &p : d->images)
        if (p.pixels && p.edge == w.edge && std::strcmp(p.name, w.name) == 0) return &p;
    Image *slot = nullptr;
    for (Image &p : d->images) if (!p.pixels) { slot = &p; break; }
    if (!slot) return nullptr;
    DoomLump lump{};
    if (!doom_wad_lump(wad, w.name, &lump) || lump.size < 8 ||
        !u16(lump.data) || !u16(lump.data + 2) ||
        u16(lump.data) > w.edge || u16(lump.data + 2) > w.edge) return nullptr;
    DoomImage decoded{};
    if (!doom_wad_patch(wad, w.name, &decoded)) return nullptr;
    const size_t bytes = static_cast<size_t>(w.edge) * w.edge * 4;
    auto *pixels = static_cast<uint8_t *>(std::calloc(bytes, 1));
    if (pixels) {
        const unsigned left = (w.edge - decoded.width) / 2;
        const unsigned top = (w.edge - decoded.height) / 2;
        for (unsigned y = 0; y < decoded.height; ++y)
            std::memcpy(pixels + (static_cast<size_t>(top + y) * w.edge + left) * 4,
                        decoded.rgba + static_cast<size_t>(y) * decoded.width * 4,
                        static_cast<size_t>(decoded.width) * 4);
        const size_t name_size = std::strlen(w.name);
        if (name_size < sizeof(slot->name)) std::memcpy(slot->name, w.name, name_size + 1);
        slot->pixels = pixels; slot->edge = w.edge; slot->height = decoded.height;
    }
    doom_image_free(&decoded);
    return pixels ? slot : nullptr;
}
extern "C" int doom_world_display_update(DoomWorldDisplay *d, DoomEngine *engine,
                                          const DoomWad *wad, const DoomWorld *world,
                                          uint32_t generation) {
    if (!d || !engine || !wad || !world || !generation || d->blocked) return 0;
    if (d->open && d->generation != generation) {
        if (!doom_world_display_close(d, engine)) return 0;
    }
    bool any = false;
    for (unsigned i = 0; i < MaxSprites; ++i) if (desired(world, i).name) { any = true; break; }
    if (!d->open) {
        if (!any) return 1;
        if (!doom_engine_world_sprites_open(engine)) return 0;
        d->generation = generation; d->open = true;
    }
    unsigned native_count = 0;
    for (unsigned i = 0; i < MaxSprites; ++i) {
        Sprite &slot = d->sprites[i];
        const Wanted w = desired(world, i);
        if (slot.token && (!w.name || w.age < slot.age || slot.edge != w.edge)) {
            if (!doom_engine_world_sprite_remove(engine, slot.token)) { d->blocked = true; return 0; }
            slot = {};
        }
        if (slot.token) ++native_count;
    }
    unsigned creates = 0, updates = 0;
    for (unsigned i = 0; i < MaxSprites; ++i) {
        Sprite &slot = d->sprites[i];
        Wanted w = desired(world, i);
        if (!w.name) continue;
        const bool change = !slot.token || std::strcmp(slot.name, w.name) != 0;
        if (change && ((!slot.token && (creates >= 2 || native_count >= MaxNativeSprites)) ||
                       (slot.token && updates >= 4))) continue;
        const Image *p = change ? picture(d, wad, w) : nullptr;
        if (change && !p) continue;
        if (w.pickup) w.position.z += static_cast<float>(change ? p->height : 0) * 0.5f;
        if (!slot.token) {
            ++creates;
            slot.token = doom_engine_world_sprite_add(engine, p->pixels, p->edge, w.position,
                                                       static_cast<float>(p->edge) / 32.0f);
            if (!slot.token) { d->blocked = true; return 0; }
            slot.edge = p->edge; ++native_count;
        } else if (change) {
            ++updates;
            if (!doom_engine_world_sprite_update(engine, slot.token, p->pixels, p->edge)) {
                d->blocked = true; return 0;
            }
        }
        if (change) {
            const size_t name_size = std::strlen(w.name);
            if (name_size < sizeof(slot.name)) std::memcpy(slot.name, w.name, name_size + 1);
        }
        slot.age = w.age;
        /* Pickups do not move. Their initial placement includes half their
         * sprite height; projectile positions are refreshed every update. */
        if (!w.pickup && !doom_engine_world_sprite_move(engine, slot.token, w.position)) {
            d->blocked = true; return 0;
        }
    }
    return 1;
}
extern "C" int doom_world_display_close(DoomWorldDisplay *d, DoomEngine *engine) {
    if (!d || !engine) return 0;
    if (d->open && !doom_engine_world_sprites_close(engine)) { d->blocked = true; return 0; }
    d->sprites = {}; d->generation = 0; d->open = false; d->blocked = false;
    for (Image &p : d->images) { std::free(p.pixels); p = {}; }
    return 1;
}
extern "C" void doom_world_display_destroy(DoomWorldDisplay *d) {
    if (!d) return;
    for (Image &p : d->images) std::free(p.pixels);
    delete d;
}
