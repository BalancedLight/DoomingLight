#ifndef DOOM_MOVEMENT_H
#define DOOM_MOVEMENT_H

#include "dluma_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Call on the game thread. Values are native base values, not Doom units. */
typedef struct DoomMovementProfile {
    double backward_max, backward_min;
    double forward_max, forward_min;
    double strafe_max, strafe_min;
    double sprint;
} DoomMovementProfile;

/* Original 35 Hz ground-motion values expressed in metres per second,
   using 32 Doom units per metre. Acceleration is per simulation tic. */
typedef struct DoomMovementTheory {
    double walk_forward_mps, run_forward_mps;
    double walk_strafe_mps, run_strafe_mps;
    double walk_forward_accel_mps_per_tic, run_forward_accel_mps_per_tic;
    double walk_strafe_accel_mps_per_tic, run_strafe_accel_mps_per_tic;
    double friction_per_tic, max_axis_mps, stop_axis_mps;
} DoomMovementTheory;

/* Construct only; never changes the game. base_units_per_mps must come from
   a measured host calibration, including active game modifiers. Matching
   directional limits does not reproduce Doom acceleration or friction. */
int doom_movement_make_profile(double base_units_per_mps,
                               DoomMovementProfile *profile,
                               DoomMovementTheory *theory);

typedef struct DoomMovement {
    DlumaPlayerV1 player;
    DlumaObjectHandle handle;
    uint32_t owned_mask;
} DoomMovement;

int doom_movement_init(DoomMovement *movement, const DlumaPlayerV1 *player);
/* Read the current effective value and allowed base-value range. The effective
   value can include host modifiers and is not necessarily the writable base. */
DlumaStatus doom_movement_sprint_info(DoomMovement *movement, double *effective,
                                      double *minimum, double *maximum);
/* The caller supplies a host-verified base value; this does not control walk,
   strafing, stamina, jumping, climbing, or camera behavior. */
DlumaStatus doom_movement_set_sprint(DoomMovement *movement, double base);
/* Apply the directional profile, disable sprint stamina consumption, and
   request the game's infinite-fitness and infinite-stamina variables.
   The caller must calibrate the values against the supported host first.
   Partially applied values are restored if any operation fails. */
DlumaStatus doom_movement_apply_profile(DoomMovement *movement,
                                        const DoomMovementProfile *profile);
/* Retry failures while a local player exists. A replacement handle is
   accepted only after the service confirms prior ownership is retired. */
DlumaStatus doom_movement_restore(DoomMovement *movement);

#ifdef __cplusplus
}
#endif
#endif
