/* Doom Mode vehicle presentation. GPL-2.0-or-later. */
#include "doom_vehicle_feedback.h"
#include <math.h>
#include <string.h>

#define WEAPON_LOWER_MS 350.0f
#define WEAPON_LOWER_PIXELS 200.0f
#define FAST_BUGGY_MPS (40.0 / 3.6)

static int is_buggy(const char *type)
{
    static const char name[] = "buggy";
    unsigned i;
    if (!type) return 0;
    for (i = 0; i < sizeof(name) - 1; ++i)
    {
        char c = type[i];
        if (!c) return 0;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c != name[i]) return 0;
    }
    return type[sizeof(name) - 1] == 0;
}
void doom_vehicle_feedback_reset(DoomVehicleFeedback *feedback)
{
    if (feedback) memset(feedback, 0, sizeof(*feedback));
}
void doom_vehicle_feedback_advance(DoomVehicleFeedback *feedback, unsigned elapsed_ms,
                                   int valid, int player_present, int vehicle_present,
                                   const char *vehicle_type, double speed_mps)
{
    int in_buggy;
    float step;
    if (!feedback) return;
    in_buggy = valid && player_present && vehicle_present && is_buggy(vehicle_type);
    feedback->fast_buggy = in_buggy && isfinite(speed_mps) && speed_mps > FAST_BUGGY_MPS;
    step = (float)elapsed_ms / WEAPON_LOWER_MS;
    if (step > 1.0f) step = 1.0f;
    if (in_buggy)
    {
        feedback->weapon_lowering += step;
        if (feedback->weapon_lowering > 1.0f) feedback->weapon_lowering = 1.0f;
    }
    else
    {
        feedback->weapon_lowering -= step;
        if (feedback->weapon_lowering < 0.0f) feedback->weapon_lowering = 0.0f;
    }
}
void doom_vehicle_feedback_present(const DoomVehicleFeedback *feedback,
                                   const DoomPlayer *player, DoomPlayer *render_player)
{
    int tier;
    if (!feedback || !player || !render_player) return;
    *render_player = *player;
    render_player->weapon_y += (int)(feedback->weapon_lowering * WEAPON_LOWER_PIXELS);
    if (!feedback->fast_buggy || player->health <= 0 || player->face == 1 ||
        player->face_index < 0 || player->face_index >= 40) return;
    tier = player->face_index / 8;
    render_player->face_index = tier * 8 + 7;
}
