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
    return 0;
}
