#include "doom_world_display.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
constexpr unsigned HitOffset = DOOM_WORLD_MAX_PROJECTILES;
constexpr unsigned ImpactOffset = HitOffset + DOOM_WORLD_MAX_BFG_HIT_FX;
constexpr unsigned PickupOffset = ImpactOffset + DOOM_WORLD_MAX_HIT_FX;
constexpr unsigned MaxSprites = PickupOffset + DOOM_WORLD_MAX_PICKUPS;
constexpr unsigned MaxImages = 48, MaxNativeSprites = 64;
struct Image {
    char name[9]{};
    uint8_t *pixels = nullptr;
    unsigned edge = 0, height = 0;
    bool mirror = false;
};
struct Sprite {
    uint32_t token = 0;
    unsigned age = 0, edge = 0, flags = 0;
    bool mirror = false;
    char name[9]{};
};
struct Wanted {
    const char *name = nullptr;
    DoomVec3 position{};
    unsigned age = 0, edge = 0, flags = 0;
    bool pickup = false, mirror = false;
};
unsigned u16(const uint8_t *p) { return unsigned(p[0]) | (unsigned(p[1]) << 8); }
Wanted desired(const DoomWorld *world, unsigned index, const DoomVec3 *viewer) {
    Wanted w{};
    if (index < DOOM_WORLD_MAX_PROJECTILES) {
        const DoomProjectile &p = world->projectiles[index];
        w.name = doom_world_projectile_sprite(&p); w.position = p.position; w.age = p.age;
        if (w.name) {
            w.flags = DOOM_WORLD_SPRITE_FULLBRIGHT;
            if (p.state == DOOM_PROJECTILE_FLYING && p.weapon == DOOM_PLASMA)
                w.flags |= DOOM_WORLD_SPRITE_LIGHT_PLASMA;
            if (p.state == DOOM_PROJECTILE_FLYING && p.weapon == DOOM_BFG)
                w.flags |= DOOM_WORLD_SPRITE_LIGHT_BFG;
        }
        w.edge = p.weapon == DOOM_BFG ? 256u : p.weapon == DOOM_ROCKET ? 128u : 64u;
        if (w.name && p.state == DOOM_PROJECTILE_FLYING && p.weapon == DOOM_ROCKET) {
            static const char *const rotation_names[8] = {
                "MISLA1", "MISLA8A2", "MISLA7A3", "MISLA6A4",
                "MISLA5", "MISLA6A4", "MISLA7A3", "MISLA8A2"
            };
            unsigned rotation = 4;
            if (viewer) {
                constexpr float Pi = 3.14159265358979323846f;
                float angle = std::atan2(p.position.y - viewer->y, p.position.x - viewer->x) -
                              std::atan2(p.direction.y, p.direction.x) + 9.0f * Pi / 8.0f;
                while (angle < 0.0f) angle += 2.0f * Pi;
                while (angle >= 2.0f * Pi) angle -= 2.0f * Pi;
                rotation = static_cast<unsigned>(angle * (4.0f / Pi));
                if (rotation > 7) rotation = 7;
            }
            w.name = rotation_names[rotation];
            w.mirror = rotation >= 1 && rotation <= 3;
        }
    } else if (index < ImpactOffset) {
        const DoomBfgHitFx &fx = world->bfg_hit_fx[index - HitOffset];
        w.name = doom_world_bfg_hit_sprite(&fx);
        w.position = fx.position; w.age = fx.age; w.edge = 64;
        w.flags = DOOM_WORLD_SPRITE_FULLBRIGHT;
    } else if (index < PickupOffset) {
        const DoomHitFx &fx = world->hit_fx[index - ImpactOffset];
        w.name = doom_world_hit_fx_sprite(&fx);
        w.position = fx.position; w.age = fx.age; w.edge = 16;
        if (fx.kind == DOOM_IMPACT_PUFF && fx.age < 4)
            w.flags = DOOM_WORLD_SPRITE_FULLBRIGHT;
    } else {
        const DoomPickup &p = world->pickups[index - PickupOffset];
        if (p.active && p.grounded) {
            w.name = doom_world_pickup_sprite(p.kind);
            if (p.kind == DOOM_DROP_GREEN_ARMOR && p.age % 13u >= 6u) {
                w.name = "ARM1B0";
                w.flags = DOOM_WORLD_SPRITE_FULLBRIGHT;
            }
        }
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
    int bilinear = 0, dynamic_lights = 0;
};

extern "C" DoomWorldDisplay *doom_world_display_create(void) {
    return new (std::nothrow) DoomWorldDisplay;
}
static const Image *picture(DoomWorldDisplay *d, const DoomWad *wad, const Wanted &w) {
    for (const Image &p : d->images)
        if (p.pixels && p.edge == w.edge && p.mirror == w.mirror && std::strcmp(p.name, w.name) == 0) return &p;
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
        for (unsigned y = 0; y < decoded.height; ++y) {
            uint8_t *out = pixels + (static_cast<size_t>(top + y) * w.edge + left) * 4;
            const uint8_t *in = decoded.rgba + static_cast<size_t>(y) * decoded.width * 4;
            if (!w.mirror) std::memcpy(out, in, static_cast<size_t>(decoded.width) * 4);
            else for (unsigned x = 0; x < decoded.width; ++x)
                std::memcpy(out + static_cast<size_t>(x) * 4,
                            in + static_cast<size_t>(decoded.width - 1 - x) * 4, 4);
        }
        const size_t name_size = std::strlen(w.name);
        if (name_size < sizeof(slot->name)) std::memcpy(slot->name, w.name, name_size + 1);
        slot->pixels = pixels; slot->edge = w.edge; slot->height = decoded.height; slot->mirror = w.mirror;
    }
    doom_image_free(&decoded);
    return pixels ? slot : nullptr;
}
extern "C" int doom_world_display_update(DoomWorldDisplay *d, DoomEngine *engine,
                                          const DoomWad *wad, const DoomWorld *world,
                                          uint32_t generation) {
    if (!d || !engine || !wad || !world || !generation || world->generation != generation) return 0;
    /* A failed close may be retried after the engine has released the old
     * renderer. Do not reuse its sprite tokens in the new generation. */
    if (d->blocked && !doom_world_display_close(d, engine)) return 0;
    const int bilinear = doom_engine_texture_filtering(engine) != 0;
    const int dynamic_lights = doom_engine_dynamic_lights(engine) != 0;
    if (d->open && (d->generation != generation || d->bilinear != bilinear || d->dynamic_lights != dynamic_lights)) {
        if (!doom_world_display_close(d, engine)) return 0;
    }
    float camera_x = 0, camera_y = 0, camera_z = 0;
    DoomVec3 viewer{};
    const DoomVec3 *viewer_ptr = nullptr;
    if (doom_engine_camera_position(engine, &camera_x, &camera_y, &camera_z)) {
        viewer = {camera_x * 32.0f, camera_z * 32.0f, camera_y * 32.0f};
        viewer_ptr = &viewer;
    }
    bool any = false;
    for (unsigned i = 0; i < MaxSprites; ++i) if (desired(world, i, viewer_ptr).name) { any = true; break; }
    if (!d->open) {
        if (!any) return 1;
        if (!doom_engine_world_sprites_open(engine)) return 0;
        d->generation = generation; d->bilinear = bilinear; d->dynamic_lights = dynamic_lights; d->open = true;
    }
    std::array<bool, MaxSprites> admitted{};
    unsigned capacity = 0;
    for (unsigned i = 0; i < MaxSprites && capacity < MaxNativeSprites; ++i) {
        if (desired(world, i, viewer_ptr).name) { admitted[i] = true; ++capacity; }
    }
    unsigned native_count = 0;
    for (unsigned i = 0; i < MaxSprites; ++i) {
        Sprite &slot = d->sprites[i];
        Wanted w = desired(world, i, viewer_ptr);
        if (!dynamic_lights) w.flags &= ~(DOOM_WORLD_SPRITE_LIGHT_PLASMA | DOOM_WORLD_SPRITE_LIGHT_BFG);
        constexpr unsigned LightMask = DOOM_WORLD_SPRITE_LIGHT_PLASMA | DOOM_WORLD_SPRITE_LIGHT_BFG;
        if (slot.token && (!admitted[i] || !w.name || w.age < slot.age || slot.edge != w.edge ||
                           ((slot.flags ^ w.flags) & LightMask))) {
            if (!doom_engine_world_sprite_remove(engine, slot.token)) { d->blocked = true; return 0; }
            slot = {};
        }
        if (slot.token) ++native_count;
    }
    unsigned creates = 0, updates = 0;
    for (unsigned i = 0; i < MaxSprites; ++i) {
        Sprite &slot = d->sprites[i];
        Wanted w = desired(world, i, viewer_ptr);
        if (!w.name || !admitted[i]) continue;
        if (!dynamic_lights) w.flags &= ~(DOOM_WORLD_SPRITE_LIGHT_PLASMA | DOOM_WORLD_SPRITE_LIGHT_BFG);
        const bool change = !slot.token || slot.mirror != w.mirror || std::strcmp(slot.name, w.name) != 0;
        if (change && ((!slot.token && (creates >= 2 || native_count >= MaxNativeSprites)) ||
                       (slot.token && updates >= 4))) continue;
        const Image *p = change ? picture(d, wad, w) : nullptr;
        if (change && !p) continue;
        /* The billboard vertices extend by the particle size in each direction.
         * Keep the pickup anchored at half its visible height. */
        const float sprite_scale = w.pickup ? 0.5f : 1.0f;
        if (w.pickup) w.position.z += static_cast<float>(change ? p->height : 0) * 0.5f;
        if (!slot.token) {
            ++creates;
            slot.token = doom_engine_world_sprite_add(engine, p->pixels, p->edge, w.position,
                                                       static_cast<float>(p->edge) / 32.0f * sprite_scale, w.flags);
            if (!slot.token) { d->blocked = true; return 0; }
            slot.edge = p->edge; slot.flags = w.flags; ++native_count;
        } else if (change) {
            ++updates;
            if (!doom_engine_world_sprite_update(engine, slot.token, p->pixels, p->edge)) {
                d->blocked = true; return 0;
            }
        }
        if (change) {
            const size_t name_size = std::strlen(w.name);
            if (name_size < sizeof(slot.name)) std::memcpy(slot.name, w.name, name_size + 1);
            slot.mirror = w.mirror;
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

