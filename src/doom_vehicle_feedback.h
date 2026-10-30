#ifndef DOOM_VEHICLE_FEEDBACK_H
#define DOOM_VEHICLE_FEEDBACK_H
#include "doom_core.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct DoomVehicleFeedback
{
    float weapon_lowering;
    int fast_buggy;
    int in_buggy;
} DoomVehicleFeedback;

void doom_vehicle_feedback_reset(DoomVehicleFeedback *feedback);
/* speed_mps is the magnitude reported by DlumaVehicleStateResult. A failed
   query is represented by valid=0; the last confirmed driver state is kept
   until a successful query or reset. vehicle_type is borrowed for this call. */
void doom_vehicle_feedback_advance(DoomVehicleFeedback *feedback, unsigned elapsed_ms,
                                   int valid, int player_present, int vehicle_present,
                                   const char *vehicle_type, double speed_mps);
/* Query after advance, before handling weapon selection and attack input.
 * The gate stays closed until the weapon is fully raised after exiting. */
int doom_vehicle_feedback_blocks_weapons(const DoomVehicleFeedback *feedback);
/* Produces a rendering snapshot without changing gameplay state. */
void doom_vehicle_feedback_present(const DoomVehicleFeedback *feedback,
                                   const DoomPlayer *player, DoomPlayer *render_player);

#ifdef __cplusplus
}
#endif
#endif
