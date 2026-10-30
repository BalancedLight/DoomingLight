#include "doom_hud.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct HudPatch { char name[9]; DoomImage image; int valid; } HudPatch;
struct DoomHud { const DoomWad *wad; HudPatch patches[256]; unsigned count; size_t image_bytes; };

DoomHud *doom_hud_create(const DoomWad *wad)
{
    DoomHud *hud;
    if (!wad) return NULL;
    hud = (DoomHud *)calloc(1, sizeof(*hud));
    if (hud) hud->wad = wad;
    return hud;
}
void doom_hud_destroy(DoomHud *hud)
{
    unsigned i;
    if (!hud) return;
    for (i = 0; i < hud->count; ++i) doom_image_free(&hud->patches[i].image);
    free(hud);
}
static const DoomImage *patch(DoomHud *hud, const char *name)
{
    unsigned i;
    HudPatch *p;
    for (i = 0; i < hud->count; ++i)
        if (!strcmp(hud->patches[i].name, name))
            return hud->patches[i].valid ? &hud->patches[i].image : NULL;
    if (hud->count == 256 || strlen(name) > 8) return NULL;
    p = &hud->patches[hud->count++];
    memcpy(p->name, name, strlen(name) + 1);
    p->valid = doom_wad_patch(hud->wad, name, &p->image);
    if (p->valid) {
        size_t bytes = (size_t)p->image.width * p->image.height * 4;
        if (bytes > 64u * 1024u * 1024u - hud->image_bytes) {
            doom_image_free(&p->image);
            p->valid = 0;
        } else hud->image_bytes += bytes;
    }
    return p->valid ? &p->image : NULL;
}
static void draw(DoomHud *hud, uint8_t *dst, const char *name, int x, int y, int offsets)
{
    const DoomImage *image = patch(hud, name);
    uint32_t ix, iy;
    if (!image) return;
    if (offsets) { x -= image->left; y -= image->top; }
    for (iy = 0; iy < image->height; ++iy) {
        int dy = y + (int)iy;
        if (dy < 0 || dy >= 200) continue;
        for (ix = 0; ix < image->width; ++ix) {
            int dx = x + (int)ix;
            const uint8_t *s;
            uint8_t *d;
            if (dx < 0 || dx >= 320) continue;
            s = image->rgba + ((size_t)iy * image->width + ix) * 4;
            if (!s[3]) continue;
            d = dst + ((size_t)dy * 320 + (size_t)dx) * 4;
            memcpy(d, s, 4);
        }
    }
}
static void number(DoomHud *hud, uint8_t *dst, int value, int right, int y, int small)
{
    char name[9];
    int digits = 0;
    if (value < 0) value = 0;
    if (value > 999) value = 999;
    do {
        int n = value % 10;
        const DoomImage *img;
        snprintf(name, sizeof(name), small ? "STYSNUM%d" : "STTNUM%d", n);
        img = patch(hud, name);
        if (!img) return;
        right -= (int)img->width;
        draw(hud, dst, name, right, y, 0);
        value /= 10;
        ++digits;
    } while (value && digits < 3);
}
int doom_hud_render(DoomHud *hud, const DoomPlayer *p, uint8_t *rgba, size_t size)
{
    static const char *const sprites[DOOM_WEAPON_COUNT] = {
        "PUNG", "PISG", "SHTG", "CHGG", "MISG", "PLSG", "BFGG", "SAWG", "SHT2"
    };
    static const int weapon_ammo[DOOM_WEAPON_COUNT] = {
        -1, DOOM_BULLETS, DOOM_SHELLS, DOOM_BULLETS, DOOM_ROCKETS,
        DOOM_CELLS, DOOM_CELLS, -1, DOOM_SHELLS
    };
    char name[9];
    int i, weapon;
    if (!hud || !p || !rgba || size < 320u * 200u * 4u) return 0;
    memset(rgba, 0, 320u * 200u * 4u);
    weapon = p->weapon;
    if (weapon >= 0 && weapon < DOOM_WEAPON_COUNT) {
        int frame = p->weapon_frame;
        if (frame < 0 || frame > 25) frame = 0;
        snprintf(name, sizeof(name), "%s%c0", sprites[weapon], 'A' + frame);
        draw(hud, rgba, name, 0, p->weapon_y - 32, 1);
    }
    if (p->flash_tics && p->flash_weapon >= 0 && p->flash_weapon < DOOM_WEAPON_COUNT) {
        static const char *const flashes[DOOM_WEAPON_COUNT] = {
            NULL, "PISF", "SHTF", "CHGF", "MISF", "PLSF", "BFGF", NULL, "SHT2"
        };
        static const int counts[DOOM_WEAPON_COUNT] = { 0, 1, 2, 2, 4, 2, 2, 0, 2 };
        int flash_weapon = p->flash_weapon;
        int frame = p->flash_frame;
        if (flashes[flash_weapon] && frame >= 0 && frame < counts[flash_weapon]) {
            if (flash_weapon == DOOM_SUPER_SHOTGUN) frame += 8;
            snprintf(name, sizeof(name), "%s%c0", flashes[flash_weapon], 'A' + frame);
            draw(hud, rgba, name, 0, p->weapon_y - 32, 1);
        }
    }
    draw(hud, rgba, "STBAR", 0, 168, 0);
    draw(hud, rgba, "STARMS", 104, 168, 0);
    if (p->health <= 0 || p->face_index == 41) memcpy(name, "STFDEAD0", 9);
    else if (p->god || p->face_index == 40) memcpy(name, "STFGOD0", 8);
    else {
        int face = p->face_index;
        int tier, expression;
        if (face < 0 || face >= 40) face = 0;
        tier = face / 8; expression = face % 8;
        if (expression < 3) snprintf(name, sizeof(name), "STFST%d%d", tier, expression);
        else if (expression == 3) snprintf(name, sizeof(name), "STFTR%d0", tier);
        else if (expression == 4) snprintf(name, sizeof(name), "STFTL%d0", tier);
        else if (expression == 5) snprintf(name, sizeof(name), "STFOUCH%d", tier);
        else if (expression == 6) snprintf(name, sizeof(name), "STFEVL%d", tier);
        else snprintf(name, sizeof(name), "STFKILL%d", tier);
    }
    draw(hud, rgba, name, 143, 168, 0);
    if (weapon >= 0 && weapon < DOOM_WEAPON_COUNT && weapon_ammo[weapon] >= 0)
        number(hud, rgba, p->ammo[weapon_ammo[weapon]], 44, 171, 0);
    number(hud, rgba, p->health, 90, 171, 0);
    draw(hud, rgba, "STTPRCNT", 90, 171, 0);
    number(hud, rgba, p->armor, 221, 171, 0);
    draw(hud, rgba, "STTPRCNT", 221, 171, 0);
    for (i = 0; i < 4; ++i) {
        static const int order[4] = { DOOM_BULLETS, DOOM_SHELLS, DOOM_CELLS, DOOM_ROCKETS };
        number(hud, rgba, p->ammo[order[i]], 288, 173 + i * 6, 1);
        number(hud, rgba, p->max_ammo[order[i]], 314, 173 + i * 6, 1);
    }
    for (i = 0; i < 6; ++i) {
        static const int slots[6] = { DOOM_PISTOL, DOOM_SHOTGUN, DOOM_CHAINGUN, DOOM_ROCKET, DOOM_PLASMA, DOOM_BFG };
        snprintf(name, sizeof(name), (p->owned_mask & (1u << slots[i])) ? "STYSNUM%d" : "STGNUM%d", i + 2);
        draw(hud, rgba, name, 111 + (i % 3) * 12, 172 + (i / 3) * 10, 0);
    }
    if (p->keys) {
        for (i = 0; i < 3; ++i) {
            snprintf(name, sizeof(name), "STKEYS%d", i);
            draw(hud, rgba, name, 239, 171 + i * 10, 0);
        }
    }
    return 1;
}
