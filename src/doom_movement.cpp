#include "doom_movement.h"

#include <cmath>
#include <cstring>

namespace {
constexpr char SprintSpeed[] = "move_sprint_speed";
constexpr const char *Names[] = {
    "move_backward_max_speed", "move_backward_min_speed",
    "move_forward_max_speed", "move_forward_min_speed",
    "move_strafe_max_speed", "move_strafe_min_speed",
    SprintSpeed, "move_sprint_stamina_consumption", "infinite_fitness", "infinite_stamina"
};
constexpr unsigned SprintIndex = 6;
constexpr unsigned StaminaCostIndex = 7;
constexpr unsigned VariableCount = sizeof(Names) / sizeof(Names[0]);

template<class T> T record()
{
    T value{};
    value.struct_size = static_cast<uint32_t>(sizeof(value));
    value.struct_version = 1;
    return value;
}

template<class T> T result_record()
{
    T value = record<T>();
    value.call = record<DlumaResult>();
    return value;
}

DlumaStringView sprint_name()
{
    return {SprintSpeed, sizeof(SprintSpeed) - 1};
}

DlumaStringView variable_name(unsigned index)
{
    return {Names[index], std::strlen(Names[index])};
}

DlumaStatus local_player(DoomMovement *movement, DlumaObjectHandle *handle)
{
    if (!movement || !handle || !movement->player.local_player)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    const auto request = record<DlumaNoRequest>();
    auto result = result_record<DlumaObjectResult>();
    char name[32]{};
    char class_name[32]{};
    result.name.data = name;
    result.name.capacity = sizeof(name);
    result.class_name.data = class_name;
    result.class_name.capacity = sizeof(class_name);
    DlumaStatus status = movement->player.local_player(movement->player.context, &request, &result);
    if (status == DLUMA_STATUS_OK) {
        if (movement->owned_mask &&
            (movement->handle.owner_id != result.handle.owner_id ||
             movement->handle.handle_id != result.handle.handle_id ||
             movement->handle.generation != result.handle.generation ||
             movement->handle.kind != result.handle.kind)) {
            /* Confirm retirement even if the installed loader predates the
             * player-state-aware handle implementation. */
            for (unsigned i = 0; i < VariableCount; ++i) {
                if (!(movement->owned_mask & (1u << i))) continue;
                auto query = record<DlumaPlayerVariableRequest>();
                auto value = result_record<DlumaPlayerVariableResult>();
                char variable[64]{};
                query.player = result.handle;
                query.name = variable_name(i);
                value.name.data = variable;
                value.name.capacity = sizeof(variable);
                status = movement->player.variable_get(movement->player.context, &query, &value);
                if (status != DLUMA_STATUS_OK) return status;
                if (value.overridden) return DLUMA_STATUS_UNAVAILABLE;
            }
            movement->owned_mask = 0;
            movement->handle = {};
        }
        *handle = result.handle;
    }
    return status;
}
}

extern "C" int doom_movement_make_profile(double base_units_per_mps,
                                           DoomMovementProfile *profile,
                                           DoomMovementTheory *theory)
{
    if (!profile || !std::isfinite(base_units_per_mps) || base_units_per_mps <= 0.0)
        return 0;
    constexpr double tick_rate = 35.0;
    constexpr double units_per_metre = 32.0;
    constexpr double friction = 0xe800 / 65536.0;
    constexpr double thrust_scale = 2048.0 / 65536.0;
    const auto acceleration = [=](double command) {
        return command * thrust_scale * tick_rate / units_per_metre;
    };
    DoomMovementTheory result{};
    result.walk_forward_accel_mps_per_tic = acceleration(25.0);
    result.run_forward_accel_mps_per_tic = acceleration(50.0);
    result.walk_strafe_accel_mps_per_tic = acceleration(24.0);
    result.run_strafe_accel_mps_per_tic = acceleration(40.0);
    result.walk_forward_mps = result.walk_forward_accel_mps_per_tic / (1.0 - friction);
    result.run_forward_mps = result.run_forward_accel_mps_per_tic / (1.0 - friction);
    result.walk_strafe_mps = result.walk_strafe_accel_mps_per_tic / (1.0 - friction);
    result.run_strafe_mps = result.run_strafe_accel_mps_per_tic / (1.0 - friction);
    result.friction_per_tic = friction;
    result.max_axis_mps = 30.0 * tick_rate / units_per_metre;
    result.stop_axis_mps = (0x1000 / 65536.0) * tick_rate / units_per_metre;
    DoomMovementProfile candidate{};
    candidate.forward_min = result.walk_forward_mps * base_units_per_mps;
    candidate.forward_max = result.run_forward_mps * base_units_per_mps;
    candidate.backward_min = candidate.forward_min;
    candidate.backward_max = candidate.forward_max;
    candidate.strafe_min = result.walk_strafe_mps * base_units_per_mps;
    candidate.strafe_max = result.run_strafe_mps * base_units_per_mps;
    candidate.sprint = candidate.forward_max;
    if (!std::isfinite(candidate.forward_max) || !std::isfinite(candidate.strafe_max))
        return 0;
    *profile = candidate;
    if (theory) *theory = result;
    return 1;
}

extern "C" int doom_movement_init(DoomMovement *movement, const DlumaPlayerV1 *player)
{
    if (!movement || !player || player->struct_size < sizeof(*player) || player->version != 1 ||
        !player->local_player || !player->variable_get || !player->variable_set ||
        !player->variable_reset)
        return 0;
    *movement = {};
    movement->player = *player;
    return 1;
}

extern "C" DlumaStatus doom_movement_sprint_info(DoomMovement *movement, double *effective,
                                                   double *minimum, double *maximum)
{
    if (!movement || !effective || !minimum || !maximum || !movement->player.variable_get)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    DlumaObjectHandle handle{};
    DlumaStatus status = local_player(movement, &handle);
    if (status != DLUMA_STATUS_OK) return status;
    auto request = record<DlumaPlayerVariableRequest>();
    auto result = result_record<DlumaPlayerVariableResult>();
    char name[sizeof(SprintSpeed)]{};
    request.player = handle;
    request.name = sprint_name();
    result.name.data = name;
    result.name.capacity = sizeof(name);
    status = movement->player.variable_get(movement->player.context, &request, &result);
    if (status != DLUMA_STATUS_OK) return status;
    if (!std::isfinite(result.value) || !std::isfinite(result.minimum) ||
        !std::isfinite(result.maximum) || result.minimum > result.maximum)
        return DLUMA_STATUS_UNAVAILABLE;
    *effective = result.value;
    *minimum = result.minimum;
    *maximum = result.maximum;
    return DLUMA_STATUS_OK;
}

extern "C" DlumaStatus doom_movement_set_sprint(DoomMovement *movement, double base)
{
    if (!movement || !movement->player.variable_get || !movement->player.variable_set || !std::isfinite(base))
        return DLUMA_STATUS_INVALID_ARGUMENT;
    DlumaObjectHandle handle{};
    DlumaStatus status = local_player(movement, &handle);
    if (status != DLUMA_STATUS_OK) return status;
    /* This capability first appeared with state-aware player ownership. */
    auto probe = record<DlumaPlayerVariableRequest>();
    auto value = result_record<DlumaPlayerVariableResult>();
    char probe_name[64]{};
    probe.player = handle;
    probe.name = variable_name(VariableCount - 2);
    value.name.data = probe_name;
    value.name.capacity = sizeof(probe_name);
    status = movement->player.variable_get(movement->player.context, &probe, &value);
    if (status != DLUMA_STATUS_OK) return status;
    auto request = record<DlumaPlayerVariableSetRequest>();
    auto result = result_record<DlumaBoolResult>();
    request.player = handle;
    request.name = sprint_name();
    request.value = base;
    status = movement->player.variable_set(movement->player.context, &request, &result);
    if (status == DLUMA_STATUS_OK && result.value) {
        movement->handle = handle;
        movement->owned_mask |= 1u << SprintIndex;
        return DLUMA_STATUS_OK;
    }
    return status == DLUMA_STATUS_OK ? DLUMA_STATUS_UNAVAILABLE : status;
}

extern "C" DlumaStatus doom_movement_restore(DoomMovement *movement)
{
    if (!movement || !movement->player.variable_reset) return DLUMA_STATUS_INVALID_ARGUMENT;
    if (!movement->owned_mask) return DLUMA_STATUS_OK;
    DlumaObjectHandle handle{};
    DlumaStatus status = local_player(movement, &handle);
    if (status != DLUMA_STATUS_OK) return status;
    DlumaStatus failure = DLUMA_STATUS_OK;
    for (unsigned i = 0; i < VariableCount; ++i) {
        if (!(movement->owned_mask & (1u << i))) continue;
        auto request = record<DlumaPlayerVariableRequest>();
        auto result = result_record<DlumaBoolResult>();
        request.player = handle;
        request.name = variable_name(i);
        status = movement->player.variable_reset(movement->player.context, &request, &result);
        if (status == DLUMA_STATUS_OK && result.value)
            movement->owned_mask &= ~(1u << i);
        else if (failure == DLUMA_STATUS_OK)
            failure = status == DLUMA_STATUS_OK ? DLUMA_STATUS_UNAVAILABLE : status;
    }
    if (!movement->owned_mask) movement->handle = {};
    return failure;
}

extern "C" DlumaStatus doom_movement_apply_profile(DoomMovement *movement,
                                                    const DoomMovementProfile *profile)
{
    if (!movement || !profile || !movement->player.variable_get || !movement->player.variable_set ||
        !movement->player.variable_reset) return DLUMA_STATUS_INVALID_ARGUMENT;
    const double values[] = {profile->backward_max, profile->backward_min,
        profile->forward_max, profile->forward_min, profile->strafe_max, profile->strafe_min,
        profile->sprint, 0.0, 1.0, 1.0};
    if (profile->backward_min > profile->backward_max ||
        profile->forward_min > profile->forward_max || profile->strafe_min > profile->strafe_max)
        return DLUMA_STATUS_INVALID_ARGUMENT;
    DlumaObjectHandle handle{};
    DlumaStatus status = local_player(movement, &handle);
    if (status != DLUMA_STATUS_OK) return status;
    /* Preflight every variable before changing the first one. */
    for (unsigned i = 0; i < VariableCount; ++i) {
        if (!std::isfinite(values[i])) return DLUMA_STATUS_INVALID_ARGUMENT;
        auto request = record<DlumaPlayerVariableRequest>();
        auto result = result_record<DlumaPlayerVariableResult>();
        char name[64]{};
        request.player = handle;
        request.name = variable_name(i);
        result.name.data = name;
        result.name.capacity = sizeof(name);
        status = movement->player.variable_get(movement->player.context, &request, &result);
        if (status != DLUMA_STATUS_OK) return status;
        if (!std::isfinite(result.minimum) || !std::isfinite(result.maximum) ||
            values[i] < result.minimum || values[i] > result.maximum)
            return DLUMA_STATUS_INVALID_ARGUMENT;
    }
    for (unsigned i = 0; i < VariableCount; ++i) {
        auto request = record<DlumaPlayerVariableSetRequest>();
        auto result = result_record<DlumaBoolResult>();
        request.player = handle;
        request.name = variable_name(i);
        request.value = values[i];
        status = movement->player.variable_set(movement->player.context, &request, &result);
        if (status != DLUMA_STATUS_OK || !result.value) {
            DlumaStatus failure = status == DLUMA_STATUS_OK ? DLUMA_STATUS_UNAVAILABLE : status;
            (void)doom_movement_restore(movement);
            return failure;
        }
        movement->handle = handle;
        movement->owned_mask |= 1u << i;
    }
    /* Other host modifiers can make a zero base nonzero in practice. */
    auto request = record<DlumaPlayerVariableRequest>();
    auto result = result_record<DlumaPlayerVariableResult>();
    char name[64]{};
    request.player = handle;
    request.name = variable_name(StaminaCostIndex);
    result.name.data = name;
    result.name.capacity = sizeof(name);
    status = movement->player.variable_get(movement->player.context, &request, &result);
    if (status != DLUMA_STATUS_OK || result.value != 0.0) {
        (void)doom_movement_restore(movement);
        return status == DLUMA_STATUS_OK ? DLUMA_STATUS_UNAVAILABLE : status;
    }
    return DLUMA_STATUS_OK;
}
