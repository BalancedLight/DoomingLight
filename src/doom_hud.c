#include "doom_hud.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct HudPatch { char name[9]; DoomImage image; int valid; } HudPatch;
typedef struct HudPaletteEntry { uint32_t key; uint8_t rgb[3], used; } HudPaletteEntry;
struct DoomHud {
    const DoomWad *wad;
    HudPatch patches[256];
    unsigned count;
    size_t image_bytes;
    double bob_phase, filtered_speed, bob_amplitude;
    double bob_target_x, bob_target_y, bob_x, bob_y, parkour_y;
    float hand_progress;
    unsigned hand_hold_ms;
    int hand_side, hand_pickup;
    HudPaletteEntry invulnerability[512];
};

void doom_hud_bob_reset(DoomHud *hud)
{
    if (!hud) return;
    hud->bob_phase = 0;
    hud->filtered_speed = hud->bob_amplitude = 0;
    hud->bob_target_x = hud->bob_target_y = 0;
    hud->bob_x = hud->bob_y = 0;
}

void doom_hud_motion_reset(DoomHud *hud)
{
    if (!hud) return;
    doom_hud_bob_reset(hud);
    hud->parkour_y = 0;
    hud->hand_progress = 0; hud->hand_hold_ms = 0; hud->hand_side = 0; hud->hand_pickup = 0;
}

float doom_hud_lowering(const DoomHud *hud)
{
    return hud ? (float)hud->parkour_y : 0.0f;
}
int doom_hud_pickup_gesture(const DoomHud *hud)
{
    return hud && hud->hand_pickup && (hud->hand_hold_ms || hud->hand_progress>0.0f);
}

void doom_hud_motion_update(DoomHud *hud, const DoomPlayer *player, unsigned elapsed_ms,
                            float horizontal_speed_mps, float lower_pixels,
                            float *offset_x, float *offset_y)
{
    const double pi = 3.14159265358979323846;
    double dt, alpha, motion_alpha, momentum, target_amplitude, half_phase;
    int active;
    if (offset_x) *offset_x = 0.0f;
    if (offset_y) *offset_y = 0.0f;
    if (!hud) return;
    if (elapsed_ms > 250u) elapsed_ms = 250u;
    dt = (double)elapsed_ms / 1000.0;
    alpha = 1.0 - exp(-dt / 0.12);
    motion_alpha = 1.0 - exp(-dt / 0.07);
    if (!isfinite(horizontal_speed_mps) || horizontal_speed_mps < 0.0f)
        horizontal_speed_mps = 0.0f;
    if (horizontal_speed_mps > 24.0f) horizontal_speed_mps = 24.0f;
    if (!isfinite(lower_pixels) || lower_pixels < 0.0f) lower_pixels = 0.0f;
    if (lower_pixels > 200.0f) lower_pixels = 200.0f;
    active = !player || (player->health > 0 && !player->switch_phase);
    hud->filtered_speed += ((double)horizontal_speed_mps - hud->filtered_speed) * alpha;
    hud->bob_phase = fmod(hud->bob_phase + dt * 35.0, 64.0);
    /* P_CalcHeight uses (momx^2 + momy^2)/4, capped at 16.
       Momentum is measured in Doom units per 35 Hz tic. */
    momentum = hud->filtered_speed * 32.0 / 35.0;
    target_amplitude = momentum >= 8.0 ? 16.0 : momentum * momentum / 4.0;
    hud->bob_amplitude += (target_amplitude - hud->bob_amplitude) * alpha;
    if (!active) {
        hud->bob_target_x = hud->bob_target_y = 0.0;
    } else if (!player || !player->cooldown) {
        half_phase = fmod(hud->bob_phase, 32.0);
        hud->bob_target_x = hud->bob_amplitude * cos(2.0 * pi * hud->bob_phase / 64.0);
        hud->bob_target_y = hud->bob_amplitude * sin(pi * half_phase / 32.0);
    }
    hud->bob_x += (hud->bob_target_x - hud->bob_x) * motion_alpha;
    hud->bob_y += (hud->bob_target_y - hud->bob_y) * motion_alpha;
    /* Ledge hangs must remain hidden even if a weapon switch is in progress. */
    if (player && player->health <= 0) lower_pixels = 0.0f;
    hud->parkour_y += ((double)lower_pixels - hud->parkour_y) * alpha;
    if (fabs(hud->parkour_y - (double)lower_pixels) < 0.25)
        hud->parkour_y = (double)lower_pixels;
    if (offset_x) *offset_x = (float)hud->bob_x;
    if (offset_y) *offset_y = (float)(hud->bob_y + hud->parkour_y);
}

void doom_hud_bob_tick(DoomHud *hud, float horizontal_speed_mps)
{
    doom_hud_motion_update(hud, NULL, 29, horizontal_speed_mps, 0.0f, NULL, NULL);
}

DoomHud *doom_hud_create(const DoomWad *wad)
{
    DoomHud *hud;
    if (!wad) return NULL;
    hud = (DoomHud *)calloc(1, sizeof(*hud));
    if (hud) {
        DoomLump palette, colormap;
        unsigned i;
        hud->wad = wad;
        if (doom_wad_lump(wad,"PLAYPAL",&palette) && palette.size>=256u*3u &&
            doom_wad_lump(wad,"COLORMAP",&colormap) && colormap.size>=33u*256u) {
            for (i=0;i<256;++i) {
                const uint8_t *color=palette.data+i*3u;
                const uint8_t *mapped=palette.data+colormap.data[32u*256u+i]*3u;
                uint32_t key=((uint32_t)color[0]<<16)|((uint32_t)color[1]<<8)|color[2];
                unsigned slot=(key^(key>>9)^(key>>17))&511u;
                while(hud->invulnerability[slot].used && hud->invulnerability[slot].key!=key)
                    slot=(slot+1)&511u;
                hud->invulnerability[slot].used=1; hud->invulnerability[slot].key=key;
                memcpy(hud->invulnerability[slot].rgb,mapped,3);
            }
        }
    }
    return hud;
}

void doom_hud_powerup_visuals(const DoomHud *hud, const DoomPlayer *player,
                              uint8_t *rgba, size_t size)
{
    unsigned x,y;
    unsigned invisibility;
    int fuzz, inverse;
    if (!hud || !player || !rgba || size<320u*200u*4u) return;
    invisibility=player->power_tics[DOOM_POWER_INVISIBILITY];
    fuzz=invisibility>128u || (invisibility&8u)!=0;
    inverse=player->invulnerability_tics>128u || (player->invulnerability_tics&8u)!=0;
    if (!fuzz && !inverse) return;
    for (y=0;y<200;++y) for(x=0;x<320;++x) {
        uint8_t *p=rgba+((size_t)y*320u+x)*4u;
        if (!p[3]) continue;
        if (fuzz) {
            /* A screen-space darkening mask reveals the world through the
             * silhouette. Noise advances at gameplay tic rate, not render FPS. */
            uint32_t noise=player->face_random*0x9e3779b9u+x*374761393u+y*668265263u;
            noise^=noise>>13;
            p[0]=p[1]=p[2]=0;
            p[3]=(noise&3u) ? (uint8_t)(64u+((noise>>8)&31u)) : 0;
        } else {
            uint32_t key=((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
            unsigned slot=(key^(key>>9)^(key>>17))&511u;
            while(hud->invulnerability[slot].used && hud->invulnerability[slot].key!=key)
                slot=(slot+1)&511u;
            if (hud->invulnerability[slot].used) memcpy(p,hud->invulnerability[slot].rgb,3);
            else {
                uint8_t gray=(uint8_t)(255u-((unsigned)p[0]*77u+(unsigned)p[1]*150u+(unsigned)p[2]*29u)/256u);
                p[0]=p[1]=p[2]=gray;
            }
        }
    }
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
typedef struct HudCanvas { uint8_t *pixels; int height, origin_y; } HudCanvas;
static void draw(DoomHud *hud, HudCanvas *canvas, const char *name, int x, int y, int offsets)
{
    const DoomImage *image = patch(hud, name);
    uint32_t ix, iy;
    if (!image) return;
    if (offsets) { x -= image->left; y -= image->top; }
    for (iy = 0; iy < image->height; ++iy) {
        int dy = y + (int)iy;
        if (dy < canvas->origin_y || dy >= canvas->origin_y + canvas->height) continue;
        for (ix = 0; ix < image->width; ++ix) {
            int dx = x + (int)ix;
            const uint8_t *s;
            uint8_t *d;
            if (dx < 0 || dx >= 320) continue;
            s = image->rgba + ((size_t)iy * image->width + ix) * 4;
            if (!s[3]) continue;
            d = canvas->pixels + ((size_t)(dy - canvas->origin_y) * 320 + (size_t)dx) * 4;
            memcpy(d, s, 4);
        }
    }
}
DoomHudHandResult doom_hud_hand_update(DoomHud *hud, unsigned elapsed_ms,
                                       int reach_side, int pickup_pulse,
                                       int interaction_active, int force_hidden,
                                       int *side, float *offset_y)
{
    int requested;
    float step;
    if (side) *side = 0;
    if (offset_y) *offset_y = 0.0f;
    if (!hud) return force_hidden ? DOOM_HUD_HAND_HIDDEN : DOOM_HUD_HAND_WEAPON;
    if (elapsed_ms > 250u) elapsed_ms = 250u;
    if (pickup_pulse) { hud->hand_hold_ms = 400u; hud->hand_pickup = 1; }
    else if (elapsed_ms >= hud->hand_hold_ms) hud->hand_hold_ms = 0;
    else hud->hand_hold_ms -= elapsed_ms;
    requested = reach_side == -1 || reach_side == 1 || interaction_active || hud->hand_hold_ms;
    if (reach_side == -1 || reach_side == 1) hud->hand_side = reach_side;
    else if (requested && !hud->hand_side) hud->hand_side = -1;
    step = (float)elapsed_ms / 180.0f;
    if (requested) {
        hud->hand_progress += step;
        if (hud->hand_progress > 1.0f) hud->hand_progress = 1.0f;
    } else {
        hud->hand_progress -= step;
        if (hud->hand_progress < 0.0f) hud->hand_progress = 0.0f;
    }
    if (requested || hud->hand_progress > 0.0f) {
        if (side) *side = hud->hand_side;
        if (offset_y) *offset_y = (1.0f - hud->hand_progress) * 74.0f;
        return DOOM_HUD_HAND_VISIBLE;
    }
    hud->hand_side = 0; hud->hand_pickup = 0;
    return force_hidden ? DOOM_HUD_HAND_HIDDEN : DOOM_HUD_HAND_WEAPON;
}
int doom_hud_render_reach(DoomHud *hud, uint8_t *weapon, size_t size, int side)
{
    const DoomImage *image;
    unsigned x, y, width, height;
    int left, top;
    if (!hud || !weapon || size < 320u * 200u * 4u || (side != -1 && side != 1)) return 0;
    image = patch(hud, "PUNGA0");
    if (!image || !image->width || !image->height || image->width > 320 || image->height > 168) return 0;
    width = image->width;
    height = image->height;
    left = side < 0 ? 0 : 320 - (int)width;
    top = 168 - (int)height;
    memset(weapon, 0, 320u * 200u * 4u);
    for (y = 0; y < height; ++y) {
        unsigned source_y = y;
        for (x = 0; x < width; ++x) {
            unsigned source_x = x;
            const uint8_t *pixel;
            if (side < 0) source_x = image->width - 1u - source_x;
            pixel = image->rgba + ((size_t)source_y * image->width + source_x) * 4u;
            if (pixel[3]) memcpy(weapon + ((size_t)(top + (int)y) * 320u + (unsigned)left + x) * 4u, pixel, 4);
        }
    }
    return 1;
}
static void number(DoomHud *hud, HudCanvas *canvas, int value, int right, int y, int small)
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
        draw(hud, canvas, name, right, y, 0);
        value /= 10;
        ++digits;
    } while (value && digits < 3);
}
static int render_layers(DoomHud *hud, const DoomPlayer *p, uint8_t *weapon_rgba,
                         uint8_t *status_rgba, uint8_t *message_rgba, const char *message_text)
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
    HudCanvas weapon_canvas = {weapon_rgba, 200, 0};
    HudCanvas status_canvas = {status_rgba, 32, 168};
    HudCanvas message_canvas = {message_rgba, 16, 0};
    if (!hud || !p || !weapon_rgba || !status_rgba) return 0;
    memset(weapon_rgba, 0, 320u * 200u * 4u);
    weapon = p->weapon;
    if (weapon >= 0 && weapon < DOOM_WEAPON_COUNT) {
        int frame = p->weapon_frame;
        if (frame < 0 || frame > 25) frame = 0;
        snprintf(name, sizeof(name), "%s%c0", sprites[weapon], 'A' + frame);
        draw(hud, &weapon_canvas, name, 0, p->weapon_y - 32, 1);
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
            draw(hud, &weapon_canvas, name, 0, p->weapon_y - 32, 1);
        }
    }
    memset(status_rgba, 0, 320u * 32u * 4u);
    draw(hud, &status_canvas, "STBAR", 0, 168, 0);
    draw(hud, &status_canvas, "STARMS", 104, 168, 0);
    if (p->health <= 0 || p->face_index == 41) memcpy(name, "STFDEAD0", 9);
    else if (p->face_index == 40) memcpy(name, "STFGOD0", 8);
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
    draw(hud, &status_canvas, name, 143, 168, 1);
    if (weapon >= 0 && weapon < DOOM_WEAPON_COUNT && weapon_ammo[weapon] >= 0)
        number(hud, &status_canvas, p->ammo[weapon_ammo[weapon]], 44, 171, 0);
    number(hud, &status_canvas, p->health, 90, 171, 0);
    draw(hud, &status_canvas, "STTPRCNT", 90, 171, 0);
    number(hud, &status_canvas, p->armor, 221, 171, 0);
    draw(hud, &status_canvas, "STTPRCNT", 221, 171, 0);
    for (i = 0; i < 4; ++i) {
        static const int order[4] = { DOOM_BULLETS, DOOM_SHELLS, DOOM_ROCKETS, DOOM_CELLS };
        number(hud, &status_canvas, p->ammo[order[i]], 288, 173 + i * 6, 1);
        number(hud, &status_canvas, p->max_ammo[order[i]], 314, 173 + i * 6, 1);
    }
    for (i = 0; i < 6; ++i) {
        static const int slots[6] = { DOOM_PISTOL, DOOM_SHOTGUN, DOOM_CHAINGUN, DOOM_ROCKET, DOOM_PLASMA, DOOM_BFG };
        snprintf(name, sizeof(name), (p->owned_mask & (1u << slots[i])) ? "STYSNUM%d" : "STGNUM%d", i + 2);
        draw(hud, &status_canvas, name, 111 + (i % 3) * 12, 172 + (i / 3) * 10, 0);
    }
    if (p->keys) {
        for (i = 0; i < 3; ++i) {
            snprintf(name, sizeof(name), "STKEYS%d", i);
            draw(hud, &status_canvas, name, 239, 171 + i * 10, 0);
        }
    }
    if (message_rgba) {
        int x = 0;
        const unsigned char *text = (const unsigned char *)message_text;
        memset(message_rgba, 0, 320u * 16u * 4u);
        while (text && *text && *text != '\n' && x < 320) {
            unsigned ch = (unsigned)toupper(*text++);
            const DoomImage *glyph;
            if (ch < 33 || ch > 95) { x += 4; continue; }
            snprintf(name, sizeof(name), "STCFN%03u", ch);
            glyph = patch(hud, name);
            if (glyph) { draw(hud, &message_canvas, name, x, 0, 0); x += (int)glyph->width; }
            else x += 4;
        }
    }
    return 1;
}
int doom_hud_render_layers(DoomHud *hud, const DoomPlayer *p,
                           uint8_t *weapon, size_t weapon_size,
                           uint8_t *status, size_t status_size,
                           uint8_t *message, size_t message_size,
                           const char *text)
{
    if (!weapon || weapon_size < 320u * 200u * 4u ||
        !status || status_size < 320u * 32u * 4u ||
        !message || message_size < 320u * 16u * 4u) return 0;
    return render_layers(hud, p, weapon, status, message, text);
}
int doom_hud_render(DoomHud *hud, const DoomPlayer *p, uint8_t *rgba, size_t size)
{
    if (!rgba || size < 320u * 200u * 4u) return 0;
    return render_layers(hud, p, rgba, rgba + 320u * 168u * 4u, NULL, NULL);
}
