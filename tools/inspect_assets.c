#define _CRT_SECURE_NO_WARNINGS
#include "doom_core.h"
#include "doom_hud.h"
#include "doom_wad.h"
#include <stdio.h>
#include <stdlib.h>

/* Write an uncompressed top-down TGA for inspecting the local IWAD assets. */
int main(int argc, char **argv)
{
    DoomWad *wad;
    DoomHud *hud;
    DoomPlayer player;
    unsigned char pixels[320 * 200 * 4];
    unsigned char header[18] = {0};
    FILE *output;
    unsigned i;
    int ok;
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "Usage: inspect_assets <IWAD> <output.tga> [weapon 0..8]\n");
        return 1;
    }
    wad = doom_wad_open(argv[1]);
    if (!wad) { fprintf(stderr, "Could not read a complete Doom IWAD.\n"); return 1; }
    hud = doom_hud_create(wad);
    doom_reset(&player, doom_wad_is_doom2(wad));
    if (argc == 4) {
        char *end;
        long weapon = strtol(argv[3], &end, 10);
        if (*end || weapon < 0 || weapon >= DOOM_WEAPON_COUNT ||
            !(player.owned_mask & (1u << weapon))) {
            fprintf(stderr, "Weapon is unavailable in this IWAD.\n");
            doom_hud_destroy(hud); doom_wad_close(wad); return 1;
        }
        player.weapon = (int)weapon;
    }
    ok = doom_hud_render(hud, &player, pixels, sizeof(pixels));
    doom_hud_destroy(hud); doom_wad_close(wad);
    if (!ok) return 1;
    for (i = 0; i < sizeof(pixels); i += 4) {
        unsigned char red = pixels[i]; pixels[i] = pixels[i + 2]; pixels[i + 2] = red;
    }
    header[2] = 2; header[12] = 64; header[13] = 1;
    header[14] = 200; header[16] = 32; header[17] = 0x28;
    output = fopen(argv[2], "wb");
    if (!output) return 1;
    ok = fwrite(header, 1, sizeof(header), output) == sizeof(header) &&
        fwrite(pixels, 1, sizeof(pixels), output) == sizeof(pixels);
    if (fclose(output)) ok = 0;
    return ok ? 0 : 1;
}
