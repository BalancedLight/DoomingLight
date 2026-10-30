#include "doom_movement.h"
#include <cassert>
#include <cmath>
#include <limits>
int main()
{
    DoomMovementProfile p{};
    DoomMovementTheory t{};
    assert(doom_movement_make_profile(1.0, &p, &t));
    assert(std::fabs(t.walk_forward_mps - 9.11458333333333) < 1e-9);
    assert(std::fabs(t.run_forward_mps - 18.2291666666667) < 1e-9);
    assert(std::fabs(t.walk_strafe_mps - 8.75) < 1e-9);
    assert(std::fabs(t.run_strafe_mps - 14.5833333333333) < 1e-9);
    assert(std::fabs(t.friction_per_tic - 0.90625) < 1e-9);
    assert(doom_movement_make_profile(0.5, &p, nullptr));
    assert(std::fabs(p.forward_max - t.run_forward_mps * 0.5) < 1e-9);
    assert(!doom_movement_make_profile(0.0, &p, nullptr));
    assert(!doom_movement_make_profile(std::numeric_limits<double>::infinity(), &p, nullptr));
    assert(!doom_movement_make_profile(1.0, nullptr, nullptr));

    const DoomMovementCalibrationCurve normal{{0.0, 0.0}, {40.0, 20.0}, 0.0, 100.0};
    const DoomMovementCalibrationCurve backward{{0.0, 0.0}, {60.0, 20.0}, 0.0, 100.0};
    DoomMovementCalibration measured{};
    measured.forward_min = measured.forward_max = normal;
    measured.backward_min = measured.backward_max = backward;
    measured.strafe_min = measured.strafe_max = measured.sprint = normal;
    assert(doom_movement_make_calibrated_profile(&measured, &p, &t));
    assert(std::fabs(p.forward_min - 2.0 * t.walk_forward_mps) < 1e-9);
    assert(std::fabs(p.backward_max - 3.0 * t.run_forward_mps) < 1e-9);
    assert(std::fabs(p.strafe_max - 2.0 * t.run_strafe_mps) < 1e-9);
    assert(std::fabs(p.sprint - 2.0 * t.run_forward_mps) < 1e-9);
    const double before = p.forward_max;
    measured.sprint.upper.speed_mps = 10.0;
    assert(!doom_movement_make_calibrated_profile(&measured, &p, nullptr));
    assert(p.forward_max == before);
    measured.sprint = normal;
    measured.strafe_min.upper.base = measured.strafe_min.lower.base;
    assert(!doom_movement_make_calibrated_profile(&measured, &p, nullptr));
    measured.strafe_min = normal;
    measured.forward_min.lower.speed_mps = std::numeric_limits<double>::quiet_NaN();
    assert(!doom_movement_make_calibrated_profile(&measured, &p, nullptr));
    return 0;
}
