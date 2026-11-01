#include "doom_combat.h"
#include "doom_hostility.h"
#include "doom_hit_geometry.h"
#include "doom_hit_resolver.h"
#include "doom_decals.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace {
struct Vec3 { float x, y, z; };
constexpr float HostDamagePerDoomUnit = 12.0f;
using GetVector = Vec3 *(__fastcall *)(void *, Vec3 *);
using GetBounds = const float *(__fastcall *)(void *);
using GetLevel = void *(__fastcall *)(void *);
using GetId = int (__fastcall *)(void *);
using GetLevelObject = void *(__fastcall *)(void *, int);
using DynamicCast = void *(__fastcall *)(void *, long, const void *, const void *, int);
using Raytrace = unsigned char (__fastcall *)(void *, unsigned char, void *, const Vec3 *,
                                               Vec3 *, unsigned short, void *, unsigned, unsigned, int);
using GiveDamage = void (__fastcall *)(void *, void *, float, float, void *, const Vec3 *,
                                        const Vec3 *, int, int, const int *, float, float, int);
struct NativeVector { void **data; uint32_t size, capacity, allocator_tag; };
struct Bounds { Vec3 center, half; };
using FindObjects = bool (__fastcall *)(void *, NativeVector *, const Bounds *, const void *, bool, int *);
using NativeFree = void (__cdecl *)(void *);
using GetControlParent = void *(__fastcall *)(void *);
bool valid(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
void trace_hitscan(const char *message) {
#if defined(DOOM_MODE_LAB)
    static unsigned emitted = 0;
    if (emitted++ < 64) OutputDebugStringA(message);
#else
    (void)message;
#endif
}
float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }
Vec3 unit(Vec3 a) {
    float n = std::sqrt(dot(a,a));
    return n > 0.0001f ? Vec3{a.x/n,a.y/n,a.z/n} : Vec3{};
}
}
struct DoomCombat {
    const char *last_reason = "not_attempted";
    DoomDamageObservation observation{};
    std::array<DoomDamageObservation,64> observations{};
    unsigned observation_head=0, observation_count=0;
    std::array<DoomHitscanImpact,128> impacts{};
    unsigned impact_head=0, impact_count=0;
    unsigned char *game = nullptr;
    HMODULE engine = nullptr;
    GetVector camera_position = nullptr, camera_forward = nullptr, camera_up = nullptr;
    GetLevel get_level = nullptr;
    GetId get_id = nullptr;
    GetLevelObject get_object = nullptr;
    Raytrace raytrace = nullptr;
    DynamicCast dynamic_cast_object = nullptr;
    GiveDamage give_damage = nullptr;
    GetVector object_position = nullptr;
    GetBounds object_bounds = nullptr;
    FindObjects find_objects = nullptr;
    NativeFree native_free = nullptr;
    GetControlParent get_parent = nullptr;
    DoomHitResolver *hit_resolver = nullptr;
    DoomDecals *decals = nullptr;
    bool decals_allowed = false;
    ULONGLONG decal_window = 0, decal_retry = 0;
    unsigned decal_count = 0;
    void *world_player = nullptr, *world_level = nullptr;
    uint32_t world_generation = 0, world_epoch = 0;
    unsigned world_trace_budget = 0;
    uint64_t world_last_target = 0, world_splash_target = 0;
    Vec3 world_last_point{}, world_last_direction{};
    int world_last_mesh = -1;
    int world_last_bone = -1, world_last_resolution = 0, world_last_reason = 0;
    float world_damage_scale = 1.0f;
    std::array<DoomTarget,DOOM_WORLD_MAX_SPLASH_TARGETS> actor_cache{};
    Vec3 actor_cache_center{};
    float actor_cache_radius = 0;
    unsigned actor_cache_count = 0;
    unsigned actor_boxes_tested = 0, actor_boxes_hit = 0;
    bool actor_cache_valid = false;
};
extern "C" DoomCombat *doom_combat_create(void *game_module, void *engine_module) {
    if (!game_module || !engine_module) return nullptr;
    auto *game = static_cast<unsigned char *>(game_module);
    auto engine = static_cast<HMODULE>(engine_module);
    static const unsigned char cast_prefix[] = {0xff,0x25,0x64,0x19,0x0c,0x00};
    static const unsigned char give_prefix[] = {0x40,0x55,0x48,0x8d,0x6c,0x24,0xf1,0x48,0x81,0xec,0xe0,0x00,0x00,0x00};
    if (std::memcmp(game+0x130A87E,cast_prefix,sizeof(cast_prefix)) ||
        std::memcmp(game+0xB19F60,give_prefix,sizeof(give_prefix)) ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1C952A8+16),".?AVIGSObject@@") ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1C952F0+16),".?AVIControlObject@@") ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1C95390+16),".?AVObjectDI@@") ||
        std::strcmp(reinterpret_cast<const char *>(game+0x1CA9818+16),".?AVActorDI@@")) return nullptr;
    auto *c = new (std::nothrow) DoomCombat;
    if (!c) return nullptr;
    c->game=game; c->engine=engine;
    c->camera_position=reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetPosition@IBaseCamera@@QEBA?BVvec3@@XZ"));
    c->camera_forward=reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetForwardVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
    c->camera_up=reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetUpVector@IBaseCamera@@QEBA?BVvec3@@XZ"));
    c->get_level=reinterpret_cast<GetLevel>(GetProcAddress(engine,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    c->get_id=reinterpret_cast<GetId>(GetProcAddress(engine,"?GetID@IGSObject@@QEBAHXZ"));
    c->get_object=reinterpret_cast<GetLevelObject>(GetProcAddress(engine,"?GetObjectByID@ILevel@@QEBAPEAVIGSObject@@H@Z"));
    c->raytrace=reinterpret_cast<Raytrace>(GetProcAddress(engine,"?Raytrace@IGSObject@@QEAAEEPEAUSCollision@@AEBVvec3@@AEAV3@GPEAVIControlObject@@IIH@Z"));
    c->dynamic_cast_object=reinterpret_cast<DynamicCast>(game+0x130A87E);
    c->give_damage=reinterpret_cast<GiveDamage>(game+0xB19F60);
    c->object_position=reinterpret_cast<GetVector>(GetProcAddress(engine,"?GetWorldPosition@IControlObject@@QEBA?AVvec3@@XZ"));
    c->object_bounds=reinterpret_cast<GetBounds>(GetProcAddress(engine,"?GetExtentsInWorld@IControlObject@@QEBAAEBVextents@@XZ"));
    c->get_parent=reinterpret_cast<GetControlParent>(GetProcAddress(engine,"?GetParent@IControlObject@@QEBAPEAV1@XZ"));
    void *find_address=reinterpret_cast<void *>(GetProcAddress(engine,"?FindObjectsInExtents@ILevel@@QEAA_NPEAV?$vector@PEAVIControlObject@@@ttl@@AEBVaabb@@PEBVCRTTI@@_NPEAH@Z"));
    static const unsigned char find_prefix[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x4c};
    HMODULE native_crt=GetModuleHandleW(L"MSVCR100.dll");
    c->native_free=native_crt ? reinterpret_cast<NativeFree>(GetProcAddress(native_crt,"free")) : nullptr;
    if (find_address==reinterpret_cast<unsigned char *>(engine)+0x2D05B0 &&
        !std::memcmp(find_address,find_prefix,sizeof(find_prefix)))
        c->find_objects=reinterpret_cast<FindObjects>(find_address);
    if (!c->camera_position || !c->camera_forward || !c->camera_up || !c->get_level ||
        !c->get_id || !c->get_object || !c->raytrace) { delete c; return nullptr; }
    c->hit_resolver=doom_hit_resolver_create(game_module,engine_module);
    return c;
}
extern "C" void doom_combat_destroy(DoomCombat *combat) {
    if (!combat) return;
    doom_decals_close(combat->decals);
    doom_hit_resolver_destroy(combat->hit_resolver);
    delete combat;
}
extern "C" void doom_combat_enable_decals(DoomCombat *c, int supported_profile) {
    if (c) c->decals_allowed=supported_profile!=0;
}
extern "C" const char *doom_combat_last_reason(const DoomCombat *combat) {
    return combat ? combat->last_reason : "combat_missing";
}
extern "C" unsigned doom_combat_actor_count(const DoomCombat *c) { return c ? c->actor_cache_count : 0; }
extern "C" unsigned doom_combat_boxes_tested(const DoomCombat *c) { return c ? c->actor_boxes_tested : 0; }
extern "C" unsigned doom_combat_boxes_hit(const DoomCombat *c) { return c ? c->actor_boxes_hit : 0; }
extern "C" unsigned doom_combat_drain_impacts(DoomCombat *c, DoomHitscanImpact *out, unsigned capacity) {
    if (!c || !out) return 0;
    unsigned count=0;
    while (count<capacity && c->impact_count) {
        out[count++]=c->impacts[c->impact_head];
        c->impact_head=(c->impact_head+1u)%static_cast<unsigned>(c->impacts.size());
        --c->impact_count;
    }
    return count;
}
extern "C" int doom_combat_last_observation(const DoomCombat *c, DoomDamageObservation *out) {
    if (!c || !out || !c->observation.sequence) return 0;
    *out=c->observation;
    return 1;
}
extern "C" unsigned doom_combat_drain_observations(DoomCombat *c, DoomDamageObservation *out, unsigned capacity) {
    if (!c || !out || !capacity) return 0;
    unsigned count=0;
    while (count<capacity && c->observation_count) {
        out[count++]=c->observations[c->observation_head];
        c->observation_head=(c->observation_head+1u)%static_cast<unsigned>(c->observations.size());
        --c->observation_count;
    }
    return count;
}
namespace {
struct ActorHit { void *object; int id; Vec3 point; float fraction; int mesh, bone, resolution, reason; };
int trace_actor(DoomCombat *, Vec3, Vec3, ActorHit *);
bool read_actor_health(DoomCombat *c, void *object, float *value) {
    __try {
        if (!c || !object || !value) return false;
        void *actor=c->dynamic_cast_object(object,0,c->game+0x1C95390,c->game+0x1CA9818,0);
        if (!actor || actor!=object) return false;
        auto **vtable=*reinterpret_cast<void ***>(actor);
        void *getter=vtable ? vtable[199] : nullptr;
        HMODULE owner=nullptr;
        if (!getter || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(getter),&owner) ||
            owner!=reinterpret_cast<HMODULE>(c->game)) return false;
        const float health=reinterpret_cast<float (__fastcall *)(void *)>(getter)(actor);
        if (!std::isfinite(health)) return false;
        *value=health; return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#if defined(DOOM_MODE_LAB)
void observe_before(DoomCombat *c, void *target, int id, int weapon, float damage, float physics_damage) {
    uint32_t sequence=c->observation.sequence+1u;
    if (!sequence) ++sequence;
    c->observation={}; c->observation.sequence=sequence;
    c->observation.weapon=weapon; c->observation.target_id=id;
    c->observation.native_damage=damage;
    c->observation.physics_damage=physics_damage;
    c->observation.before_valid=read_actor_health(c,target,&c->observation.before) ? 1 : 0;
}
void observe_after(DoomCombat *c, void *level, void *original, int id) {
    __try {
        void *gs=c->get_object(level,id);
        if (!gs || c->get_id(gs)!=id || c->get_level(gs)!=level) { c->observation.removed=1; return; }
        void *target=c->dynamic_cast_object(gs,0,c->game+0x1C952A8,c->game+0x1C95390,0);
        if (!target || target!=original || static_cast<unsigned char *>(target)+40!=gs) {
            c->observation.removed=1; return;
        }
        c->observation.after_valid=read_actor_health(c,target,&c->observation.after) ? 1 : 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { c->observation.after_valid=0; }
}
void queue_observation(DoomCombat *c) {
    if (c->observation_count==c->observations.size()) {
        c->observation_head=(c->observation_head+1u)%static_cast<unsigned>(c->observations.size());
        --c->observation_count;
    }
    unsigned slot=(c->observation_head+c->observation_count)%static_cast<unsigned>(c->observations.size());
    c->observations[slot]=c->observation;
    ++c->observation_count;
}
#endif
void *object_from_control(DoomCombat *c, void *control) {
    for (unsigned i=0;control && i<8;++i) {
        void *object=c->dynamic_cast_object(control,0,c->game+0x1C952F0,c->game+0x1C95390,0);
        if (object) return object;
        if (!c->get_parent) break;
        void *parent=c->get_parent(control);
        if (parent==control) break;
        control=parent;
    }
    return nullptr;
}
void queue_hitscan_impact(DoomCombat *c, int weapon, Vec3 point, Vec3 direction, DoomImpactKind kind) {
    if (weapon==DOOM_FIST || weapon==DOOM_CHAINSAW || c->impact_count>=c->impacts.size()) return;
    if (kind==DOOM_IMPACT_PUFF) {
        point.x-=direction.x*0.04f; point.y-=direction.y*0.04f; point.z-=direction.z*0.04f;
    }
    if (!valid(point)) return;
    unsigned slot=(c->impact_head+c->impact_count)%static_cast<unsigned>(c->impacts.size());
    c->impacts[slot]={kind,{point.x*DOOM_WORLD_UNITS_PER_METRE,point.z*DOOM_WORLD_UNITS_PER_METRE,
                            point.y*DOOM_WORLD_UNITS_PER_METRE}};
    ++c->impact_count;
}
}
static int fire_hitscan(DoomCombat *c, void *player, void *level, void *camera,
                       const DoomEvent *event, float scale) {
    Vec3 pos{}, forward{}, up{};
    Vec3 *p=c->camera_position(camera,&pos), *f=c->camera_forward(camera,&forward),
         *u=c->camera_up(camera,&up);
    if (!p || !f || !u) { c->last_reason="camera_vector_null"; return 0; }
    if (!valid(*p)) { c->last_reason="camera_position_invalid"; return 0; }
    if (!valid(*f)) { c->last_reason="camera_forward_invalid"; return 0; }
    if (!valid(*u)) { c->last_reason="camera_up_invalid"; return 0; }
    pos=*p; forward=unit({-f->x,-f->y,-f->z}); up=unit(*u);
    Vec3 right=unit(cross(up,forward));
    if (dot(forward,forward)<0.9f) { c->last_reason="camera_forward_degenerate"; return 0; }
    if (dot(right,right)<0.9f) { c->last_reason="camera_basis_degenerate"; return 0; }
    float angle=static_cast<float>(event->spread)*(6.283185307179586f/16384.0f);
    float slope=static_cast<float>(event->vertical_spread)/2048.0f;
    Vec3 direction=unit({forward.x*std::cos(angle)+right.x*std::sin(angle)+up.x*slope,
                         forward.y*std::cos(angle)+right.y*std::sin(angle)+up.y*slope,
                         forward.z*std::cos(angle)+right.z*std::sin(angle)+up.z*slope});
    if (dot(direction,direction)<0.9f) { c->last_reason="shot_direction_degenerate"; return 0; }
    const float range=event->weapon==DOOM_FIST ? 2.0f :
                      event->weapon==DOOM_CHAINSAW ? 65.0f/32.0f : 64.0f;
    Vec3 end{pos.x+direction.x*range,pos.y+direction.y*range,pos.z+direction.z*range};
    alignas(16) std::array<unsigned char,256> collision{};
    int mesh=-1; std::memcpy(collision.data()+32,&mesh,sizeof(mesh));
    auto *actor=static_cast<unsigned char *>(player);
    const bool world_hit=c->raytrace(actor+40,0x27,collision.data(),&pos,&end,0,actor+24,0,0,4044024)!=0;
    if (!valid(end)) { c->last_reason="impact_position_invalid"; return 0; }
    ActorHit actor_hit{};
    const bool found_actor=trace_actor(c,pos,end,&actor_hit)>0;
    if (world_hit && !found_actor && c->decals_allowed &&
        event->weapon!=DOOM_FIST && event->weapon!=DOOM_CHAINSAW &&
        c->world_player==player && c->world_level==level && c->get_level(actor+40)==level) {
        const ULONGLONG now=GetTickCount64();
        if (now-c->decal_window>=1000) { c->decal_window=now; c->decal_count=0; }
        if (c->decal_count<64) {
            if (!c->decals && now>=c->decal_retry) {
                c->decals=doom_decals_open(c->engine);
                if (!c->decals) c->decal_retry=now+1000;
            }
            Vec3 normal{}; std::memcpy(&normal,collision.data(),sizeof(normal));
            const float point[3]={end.x,end.y,end.z}, surface[3]={normal.x,normal.y,normal.z};
            if (doom_decals_add(c->decals,level,point,surface)) ++c->decal_count;
        }
    }
    void *hit=nullptr; std::memcpy(&hit,collision.data()+24,sizeof(hit));
    std::memcpy(&mesh,collision.data()+32,sizeof(mesh));
    void *target=nullptr;
    if (found_actor) { target=actor_hit.object; end=actor_hit.point; mesh=actor_hit.mesh; }
    else if (world_hit && hit) target=object_from_control(c,hit);
    else {
        if (world_hit) queue_hitscan_impact(c,event->weapon,end,direction,DOOM_IMPACT_PUFF);
        c->last_reason=!c->actor_cache_valid ? "actor_query_unavailable" :
            !c->actor_cache_count ? "actor_query_empty" :
            !c->actor_boxes_tested ? "actor_candidates_rejected" :
            !c->actor_boxes_hit ? "actor_bounds_missed" : "actor_not_alive";
        return 1;
    }
    if (!target) { if (world_hit) queue_hitscan_impact(c,event->weapon,end,direction,DOOM_IMPACT_PUFF); c->last_reason="target_cast_failed"; trace_hitscan("Doom hitscan: target cast failed\n"); return 1; }
    if (target==player) { c->last_reason="target_is_player"; trace_hitscan("Doom hitscan: hit local player\n"); return 1; }
    auto *target_gs=static_cast<unsigned char *>(target)+40;
    if (c->get_level(actor+40)!=level || c->get_level(target_gs)!=level) {
        c->last_reason="target_level_mismatch"; trace_hitscan("Doom hitscan: target level mismatch\n"); return 0;
    }
    int id=c->get_id(target_gs);
    if (id<0) { c->last_reason="target_id_negative"; trace_hitscan("Doom hitscan: target ID negative\n"); return 0; }
    if (c->get_object(level,id)!=target_gs) {
        c->last_reason="target_id_mismatch"; trace_hitscan("Doom hitscan: target ID mismatch\n"); return 0;
    }
    void *hit_actor=c->dynamic_cast_object(target,0,c->game+0x1C95390,c->game+0x1CA9818,0);
    queue_hitscan_impact(c,event->weapon,end,direction,hit_actor ? DOOM_IMPACT_BLOOD : DOOM_IMPACT_PUFF);
    int invalid_item=-1;
    const float damage=static_cast<float>(event->damage)*HostDamagePerDoomUnit*scale;
    if (!std::isfinite(damage) || damage<=0 || damage>100000.0f) { c->last_reason="damage_invalid"; return 0; }
    /* Player engine EDamageType: cut=1, bullet=2, punch1=12. */
    const int damage_type=event->weapon==DOOM_FIST ? 12 :
                          event->weapon==DOOM_CHAINSAW ? 1 : 2;
#if defined(DOOM_MODE_LAB)
    observe_before(c,target,id,event->weapon,damage,0.0f);
    c->observation.mesh_element=mesh;
    c->observation.hit_bone=found_actor ? actor_hit.bone : -1;
    c->observation.model_resolution=found_actor ? actor_hit.resolution : 0;
    c->observation.model_reason=found_actor ? actor_hit.reason : 0;
#endif
    c->give_damage(player,target,damage,0.0f,actor+40,&end,&direction,mesh,damage_type,
                   &invalid_item,0.0f,0.0f,0);
#if defined(DOOM_MODE_LAB)
    observe_after(c,level,target,id);
    queue_observation(c);
#endif
    c->last_reason="damage_dispatched";
    trace_hitscan("Doom hitscan: damage dispatched\n");
    return 1;
}
extern "C" int doom_combat_hitscan(DoomCombat *c, void *player, void *level, void *camera,
                                   const DoomEvent *event, float scale) {
    if (c) c->last_reason="invalid_hitscan_arguments";
    if (!c || !player || !level || !camera || !event || event->type!=DOOM_EVENT_ATTACK ||
        (event->weapon!=DOOM_FIST && event->weapon!=DOOM_CHAINSAW &&
         event->weapon!=DOOM_PISTOL && event->weapon!=DOOM_CHAINGUN &&
         event->weapon!=DOOM_SHOTGUN && event->weapon!=DOOM_SUPER_SHOTGUN) ||
        !std::isfinite(scale) || scale<=0 || scale>1000.0f)
        return 0;
    __try { return fire_hitscan(c,player,level,camera,event,scale); }
    __except(EXCEPTION_EXECUTE_HANDLER) { c->last_reason="native_exception"; return 0; }
}
namespace {
Vec3 engine_position(DoomVec3 p) {
    return {p.x/DOOM_WORLD_UNITS_PER_METRE,p.z/DOOM_WORLD_UNITS_PER_METRE,
            p.y/DOOM_WORLD_UNITS_PER_METRE};
}
DoomVec3 world_position(Vec3 p) {
    return {p.x*DOOM_WORLD_UNITS_PER_METRE,p.z*DOOM_WORLD_UNITS_PER_METRE,
            p.y*DOOM_WORLD_UNITS_PER_METRE};
}
int trace_segment(DoomCombat *c, DoomVec3 from, DoomVec3 to, DoomTrace *trace, bool actors=true) {
    if (!c || !c->world_player || !c->world_level || !trace) return 0;
    if (!c->world_trace_budget) return 0;
    --c->world_trace_budget;
    Vec3 start=engine_position(from), end=engine_position(to);
    if (!valid(start) || !valid(end)) return 0;
    Vec3 delta{end.x-start.x,end.y-start.y,end.z-start.z};
    float length2=dot(delta,delta);
    if (!std::isfinite(length2) || length2<=0 || length2>4194304.0f) return 0;
    std::memset(trace,0,sizeof(*trace));
    c->world_last_target=0;
    c->world_last_bone=-1; c->world_last_resolution=0; c->world_last_reason=0;
    alignas(16) std::array<unsigned char,256> collision{};
    int mesh=-1; std::memcpy(collision.data()+32,&mesh,sizeof(mesh));
    auto *actor=static_cast<unsigned char *>(c->world_player);
    if (c->get_level(actor+40)!=c->world_level) return 0;
    bool hit=c->raytrace(actor+40,0x27,collision.data(),&start,&end,0,actor+24,0,0,4044024)!=0;
    if (!valid(end) || c->get_level(actor+40)!=c->world_level) return 0;
    ActorHit actor_hit{};
    if (actors && trace_actor(c,start,end,&actor_hit)>0) {
        trace->hit=1;
        trace->point=world_position(actor_hit.point);
        trace->target=(static_cast<uint64_t>(c->world_epoch)<<32) |
                      (static_cast<uint64_t>(static_cast<uint32_t>(actor_hit.id))+1u);
        c->world_last_target=trace->target;
        c->world_last_point=actor_hit.point;
        c->world_last_direction=unit(delta);
        c->world_last_mesh=actor_hit.mesh;
        c->world_last_bone=actor_hit.bone; c->world_last_resolution=actor_hit.resolution;
        c->world_last_reason=actor_hit.reason;
        return 1;
    }
    trace->hit=hit ? 1 : 0;
    trace->point=world_position(end);
    if (!hit) return 1;
    void *control=nullptr; std::memcpy(&control,collision.data()+24,sizeof(control));
    if (!control) return 1;
    void *target=object_from_control(c,control);
    if (!target || target==c->world_player) return 1;
    auto *gs=static_cast<unsigned char *>(target)+40;
    if (c->get_level(gs)!=c->world_level) return 0;
    int id=c->get_id(gs);
    if (id<0 || c->get_object(c->world_level,id)!=gs) return 0;
    trace->target=(static_cast<uint64_t>(c->world_epoch)<<32) |
                  (static_cast<uint64_t>(static_cast<uint32_t>(id))+1u);
    std::memcpy(&c->world_last_mesh,collision.data()+32,sizeof(c->world_last_mesh));
    c->world_last_target=trace->target;
    c->world_last_point=end;
    c->world_last_direction=unit(delta);
    return 1;
}
int sweep_segment(DoomCombat *c, DoomVec3 from, DoomVec3 to, float radius, DoomTrace *trace) {
    if (!c || !trace || !std::isfinite(radius) || radius<0) return 0;
    if (radius==0.0f) return trace_segment(c,from,to,trace);
#if defined(DOOM_MODE_RUNTIME)
    /* Candidate volume approximation: one forward and eight perimeter rays.
       Thin geometry between samples can still be missed. */
    if (radius>32.0f || c->world_trace_budget<9) return 0;
    Vec3 motion{to.x-from.x,to.y-from.y,to.z-from.z};
    float length2=dot(motion,motion);
    if (!std::isfinite(length2) || length2<0.000001f) return 0;
    Vec3 direction=unit(motion);
    Vec3 basis=std::fabs(direction.z)<0.9f ? Vec3{0,0,1} : Vec3{0,1,0};
    Vec3 right=unit(cross(direction,basis));
    Vec3 up=unit(cross(right,direction));
    if (dot(right,right)<0.9f || dot(up,up)<0.9f) return 0;
    const float h=0.70710678118f;
    const float offsets[9][2]={{0,0},{1,0},{-1,0},{0,1},{0,-1},
                                {h,h},{h,-h},{-h,h},{-h,-h}};
    DoomTrace best{}; bool found=false; float best_fraction=2.0f;
    uint64_t best_target=0; Vec3 best_point{},best_direction{}; int best_mesh=-1, best_bone=-1, best_resolution=0, best_reason=0;
    for (unsigned i=0;i<9;++i) {
        Vec3 shift=i==0 ? Vec3{direction.x*radius,direction.y*radius,direction.z*radius}
            : Vec3{radius*(right.x*offsets[i][0]+up.x*offsets[i][1]),
                   radius*(right.y*offsets[i][0]+up.y*offsets[i][1]),
                   radius*(right.z*offsets[i][0]+up.z*offsets[i][1])};
        DoomVec3 a{from.x+shift.x,from.y+shift.y,from.z+shift.z};
        DoomVec3 b{to.x+shift.x,to.y+shift.y,to.z+shift.z};
        DoomTrace sample{};
        if (!trace_segment(c,a,b,&sample)) return 0;
        if (!sample.hit) continue;
        Vec3 travelled{sample.point.x-a.x,sample.point.y-a.y,sample.point.z-a.z};
        float fraction=dot(travelled,motion)/length2;
        if (!std::isfinite(fraction)) return 0;
        if (fraction<0) fraction=0;
        if (fraction>1) fraction=1;
        if (fraction<best_fraction) {
            found=true; best_fraction=fraction; best=sample;
            best.point={from.x+motion.x*fraction,from.y+motion.y*fraction,from.z+motion.z*fraction};
            best_target=c->world_last_target; best_point=c->world_last_point;
            best_direction=c->world_last_direction; best_mesh=c->world_last_mesh;
            best_bone=c->world_last_bone; best_resolution=c->world_last_resolution;
            best_reason=c->world_last_reason;
        }
    }
    if (!found) { std::memset(trace,0,sizeof(*trace)); trace->point=to; return 1; }
    *trace=best; c->world_last_target=best_target; c->world_last_point=best_point;
    c->world_last_direction=best_direction; c->world_last_mesh=best_mesh;
    c->world_last_bone=best_bone; c->world_last_resolution=best_resolution;
    c->world_last_reason=best_reason;
    return 1;
#else
    (void)from; (void)to;
    return 0;
#endif
}
int world_sweep(void *ctx, DoomVec3 from, DoomVec3 to, float radius, DoomTrace *trace) {
    __try { return sweep_segment(static_cast<DoomCombat *>(ctx),from,to,radius,trace); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int world_ground(void *ctx, DoomVec3 from, DoomVec3 to, DoomTrace *trace) {
    __try { return trace_segment(static_cast<DoomCombat *>(ctx),from,to,trace,false); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
int world_aim(void *ctx, DoomVec3 origin, DoomVec3 direction, float range, DoomTrace *trace) {
    if (!std::isfinite(range) || range<=0 || range>65536.0f ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z))
        return 0;
    float magnitude=std::sqrt(direction.x*direction.x+direction.y*direction.y+direction.z*direction.z);
    if (!std::isfinite(magnitude) || magnitude<0.0001f) return 0;
    float scale=range/magnitude;
    DoomVec3 end{origin.x+direction.x*scale,origin.y+direction.y*scale,origin.z+direction.z*scale};
    __try { return trace_segment(static_cast<DoomCombat *>(ctx),origin,end,trace); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
bool object_geometry(DoomCombat *c, void *target, Vec3 *center, float *radius) {
    auto *control=static_cast<unsigned char *>(target)+24;
    Vec3 position{};
    Vec3 *p=c->object_position(control,&position);
    const float *b=c->object_bounds(control);
    if (p!=&position || !b || !valid(position)) return false;
    float bounds[6]; std::memcpy(bounds,b,sizeof(bounds));
    for (float n : bounds) if (!std::isfinite(n)) return false;
    if (bounds[0]>bounds[3] || bounds[1]>bounds[4] || bounds[2]>bounds[5]) return false;
    /* GetExtentsInWorld already applies the object's world transform. */
    *center={(bounds[0]+bounds[3])*0.5f,
             (bounds[1]+bounds[4])*0.5f,
             (bounds[2]+bounds[5])*0.5f};
    float x=(bounds[3]-bounds[0])*0.5f, z=(bounds[5]-bounds[2])*0.5f;
    *radius=(x>z ? x : z)*DOOM_WORLD_UNITS_PER_METRE;
    return valid(*center) && std::isfinite(*radius) && *radius>=0 && *radius<=65536.0f;
}
int collect_nearby(DoomCombat *c, DoomVec3 origin, float radius,
                   DoomTarget *out, unsigned capacity, unsigned *count, bool include_props) {
    NativeVector matches{nullptr,0,0,31};
    int success=0;
    float edge_distances[DOOM_WORLD_MAX_SPLASH_TARGETS]{};
    unsigned char actor_priority[DOOM_WORLD_MAX_SPLASH_TARGETS]{};
    Vec3 center=engine_position(origin);
    if (!valid(center) || !std::isfinite(radius) || radius<=0 || radius>4096.0f ||
        !c->world_player || !c->world_level || !c->find_objects || !c->native_free ||
        !c->object_position || !c->object_bounds || !out || !count ||
        !capacity || capacity>DOOM_WORLD_MAX_SPLASH_TARGETS) return 0;
    *count=0;
    float r=radius/DOOM_WORLD_UNITS_PER_METRE;
    Bounds bounds{center,{r,r,r}};
    __try {
        auto *player=static_cast<unsigned char *>(c->world_player);
        if (c->get_level(player+40)!=c->world_level) __leave;
        (void)c->find_objects(c->world_level,&matches,&bounds,nullptr,false,nullptr);
        if (matches.size>matches.capacity || matches.size>4096 ||
            (matches.size && !matches.data)) __leave;
        for (uint32_t i=0; i<matches.size && (include_props || *count<capacity); ++i) {
            void *control=matches.data[i];
            if (!control) continue;
            void *actor=c->dynamic_cast_object(control,0,c->game+0x1C952F0,c->game+0x1CA9818,0);
            void *target=c->dynamic_cast_object(control,0,c->game+0x1C952F0,c->game+0x1C95390,0);
            if (!target || target==c->world_player || (!actor && !include_props)) continue;
            auto *gs=static_cast<unsigned char *>(target)+40;
            if (c->get_level(gs)!=c->world_level) continue;
            int id=c->get_id(gs);
            if (id<0 || c->get_object(c->world_level,id)!=gs) continue;
            if (include_props && actor) {
                float health=0;
                if (!read_actor_health(c,target,&health) || health<=0 ||
                    doom_hostility_query(c->game,target,c->world_player)!=1) continue;
            }
            const uint64_t handle=(static_cast<uint64_t>(c->world_epoch)<<32) |
                                  (static_cast<uint64_t>(static_cast<uint32_t>(id))+1u);
            bool duplicate=false;
            for (unsigned j=0;j<*count;++j)
                if (out[j].handle==handle) { duplicate=true; break; }
            if (duplicate) continue;
            Vec3 object_center{}; float object_radius=0;
            if (!object_geometry(c,target,&object_center,&object_radius)) continue;
            DoomTarget item{};
            item.handle=handle; item.position=world_position(object_center); item.radius=object_radius;
            if (!include_props) { out[(*count)++]=item; continue; }
            /* The core measures blast falloff in the horizontal Doom plane. */
            if (object_radius>128.0f) continue;
            float dx=std::fabs(object_center.x-center.x)*DOOM_WORLD_UNITS_PER_METRE;
            float dy=std::fabs(object_center.z-center.z)*DOOM_WORLD_UNITS_PER_METRE;
            float edge=std::max(dx,dy)-object_radius;
            if (edge<0) edge=0;
            if (!std::isfinite(edge) || edge>=radius) continue;
            const unsigned char priority=actor ? 1u : 0u;
            unsigned insert=0;
            while (insert<*count &&
                   (actor_priority[insert]>priority ||
                    (actor_priority[insert]==priority && edge_distances[insert]<=edge)))
                ++insert;
            if (insert>=capacity) continue;
            if (*count<capacity) ++*count;
            for (unsigned j=*count-1; j>insert; --j) {
                out[j]=out[j-1]; edge_distances[j]=edge_distances[j-1];
                actor_priority[j]=actor_priority[j-1];
            }
            out[insert]=item; edge_distances[insert]=edge; actor_priority[insert]=priority;
        }
        if (c->get_level(player+40)==c->world_level) success=1;
    } __finally {
        if (matches.data) c->native_free(matches.data);
    }
    return success;
}
int trace_actor(DoomCombat *c, Vec3 from, Vec3 to, ActorHit *out) {
    if (c) c->actor_boxes_tested=c->actor_boxes_hit=0;
    if (!c || !out || !c->world_player || !c->world_level || !c->object_bounds ||
        !valid(from) || !valid(to)) return 0;
    Vec3 delta{to.x-from.x,to.y-from.y,to.z-from.z};
    const float length=std::sqrt(dot(delta,delta));
    if (!std::isfinite(length) || length<0.0001f || length>2048.0f) return 0;
    const auto covered=[&](Vec3 p) {
        const float r=c->actor_cache_radius-2.0f;
        return c->actor_cache_valid && r>=0 &&
            std::fabs(p.x-c->actor_cache_center.x)<=r &&
            std::fabs(p.y-c->actor_cache_center.y)<=r &&
            std::fabs(p.z-c->actor_cache_center.z)<=r;
    };
    if (!covered(from) || !covered(to)) {
        c->actor_cache_center={(from.x+to.x)*0.5f,(from.y+to.y)*0.5f,(from.z+to.z)*0.5f};
        c->actor_cache_radius=length*0.5f+8.0f;
        c->actor_cache_count=0; c->actor_cache_valid=false;
        if (!collect_nearby(c,world_position(c->actor_cache_center),
                c->actor_cache_radius*DOOM_WORLD_UNITS_PER_METRE,c->actor_cache.data(),
                static_cast<unsigned>(c->actor_cache.size()),&c->actor_cache_count,false)) return 0;
        c->actor_cache_valid=true;
    }
    float best=2.0f; void *best_object=nullptr; int best_id=-1;
    Vec3 best_point{}; int best_mesh=-1, best_bone=-1, best_resolution=0, best_reason=0;
    const float begin[3]={from.x,from.y,from.z}, end[3]={to.x,to.y,to.z};
    for (unsigned i=0;i<c->actor_cache_count;++i) {
        uint64_t handle=c->actor_cache[i].handle;
        if (static_cast<uint32_t>(handle>>32)!=c->world_epoch) continue;
        uint32_t encoded=static_cast<uint32_t>(handle);
        if (!encoded || encoded>static_cast<uint32_t>(INT32_MAX)+1u) continue;
        int id=static_cast<int>(encoded-1u);
        void *gs=c->get_object(c->world_level,id);
        if (!gs || c->get_id(gs)!=id || c->get_level(gs)!=c->world_level) continue;
        void *target=c->dynamic_cast_object(gs,0,c->game+0x1C952A8,c->game+0x1C95390,0);
        if (!target || target==c->world_player || static_cast<unsigned char *>(target)+40!=gs) continue;
        const float *native_bounds=c->object_bounds(static_cast<unsigned char *>(target)+24);
        if (!native_bounds) continue;
        float bounds[6]; std::memcpy(bounds,native_bounds,sizeof(bounds));
        float fraction=0;
        ++c->actor_boxes_tested;
        if (!doom_segment_box(begin,end,bounds,0,&fraction) || fraction>=best) continue;
        ++c->actor_boxes_hit;
        float health=0;
        if (!read_actor_health(c,target,&health) || health<=0) continue;
        Vec3 point{from.x+delta.x*fraction,from.y+delta.y*fraction,from.z+delta.z*fraction};
        int mesh=-1, bone=-1;
        DoomResolvedHit resolved{};
        const int resolution=doom_hit_resolver_trace(c->hit_resolver,target,begin,end,&resolved);
        if (resolution==DOOM_HIT_RESOLVED) {
            point={resolved.point[0],resolved.point[1],resolved.point[2]};
            const Vec3 offset{point.x-from.x,point.y-from.y,point.z-from.z};
            const float model_fraction=dot(offset,delta)/(length*length);
            if (!std::isfinite(model_fraction) || model_fraction<0 || model_fraction>1 || model_fraction>=best)
                continue;
            fraction=model_fraction; mesh=resolved.mesh_element; bone=static_cast<int>(resolved.bone);
        }
        best=fraction; best_object=target; best_id=id; best_point=point;
        best_mesh=mesh; best_bone=bone; best_resolution=resolution;
        best_reason=c->hit_resolver ? static_cast<int>(resolved.reason) : -1;
    }
    if (!best_object) return 0;
    out->object=best_object; out->id=best_id; out->fraction=best;
    out->point=best_point; out->mesh=best_mesh; out->bone=best_bone; out->resolution=best_resolution;
    out->reason=best_reason;
    return 1;
}
int world_nearby(void *ctx, DoomVec3 origin, float radius,
                 DoomTarget *out, unsigned capacity, unsigned *count) {
    __try { return collect_nearby(static_cast<DoomCombat *>(ctx),origin,radius,out,capacity,count,true); }
    __except(EXCEPTION_EXECUTE_HANDLER) { if (count) *count=0; return 0; }
}
int visible_target(DoomCombat *c, DoomVec3 from, uint64_t handle) {
    if (!c || !c->world_level || !c->object_position || !c->object_bounds ||
        !handle || static_cast<uint32_t>(handle>>32)!=c->world_epoch) return -1;
    uint32_t encoded=static_cast<uint32_t>(handle);
    if (!encoded || encoded>static_cast<uint32_t>(INT32_MAX)+1u) return -1;
    int id=static_cast<int>(encoded-1u);
    void *gs=c->get_object(c->world_level,id);
    if (!gs || c->get_id(gs)!=id || c->get_level(gs)!=c->world_level) return -1;
    void *target=c->dynamic_cast_object(gs,0,c->game+0x1C952A8,c->game+0x1C95390,0);
    if (!target || static_cast<unsigned char *>(target)+40!=gs) return -1;
    Vec3 center{}; float radius=0;
    if (!object_geometry(c,target,&center,&radius)) return -1;
    DoomTrace trace{};
    if (!trace_segment(c,from,world_position(center),&trace,false)) return -1;
    if (!trace.hit) {
        Vec3 start=engine_position(from);
        c->world_last_target=handle; c->world_last_point=center;
        c->world_last_direction=unit({center.x-start.x,center.y-start.y,center.z-start.z});
        c->world_last_mesh=-1;
        /* The visibility ray ends inside the target and may not return its
         * model element. Resolve the actual body part without changing which
         * target receives splash damage. */
        const float begin[3]={start.x,start.y,start.z};
        const float end[3]={center.x,center.y,center.z};
        DoomResolvedHit resolved{};
        const int resolution=doom_hit_resolver_trace(c->hit_resolver,target,begin,end,&resolved);
        c->world_last_resolution=resolution;
        c->world_last_reason=c->hit_resolver ? static_cast<int>(resolved.reason) : -1;
        if (resolution==DOOM_HIT_RESOLVED) {
            c->world_last_mesh=resolved.mesh_element;
            c->world_last_bone=static_cast<int>(resolved.bone);
            c->world_last_point={resolved.point[0],resolved.point[1],resolved.point[2]};
        }
        return 1;
    }
    return trace.target==handle ? 1 : 0;
}
int world_visible(void *ctx, DoomVec3 from, uint64_t target) {
    __try {
        DoomCombat *c=static_cast<DoomCombat *>(ctx);
        if (!c) return -1;
        c->world_splash_target=0;
        int result=visible_target(c,from,target);
        if (result==1) c->world_splash_target=target;
        return result;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int damage_target(DoomCombat *c, uint64_t handle, int amount, DoomWeapon weapon) {
    const bool splash=c && handle && handle==c->world_splash_target;
    if (c) c->world_splash_target=0;
    if (!c || !c->world_player || !c->world_level || !handle ||
        handle!=c->world_last_target || static_cast<uint32_t>(handle>>32)!=c->world_epoch ||
        amount<=0 || amount>100000) return 0;
    int damage_type=2;
    if (weapon!=DOOM_PISTOL && weapon!=DOOM_CHAINGUN &&
        weapon!=DOOM_SHOTGUN && weapon!=DOOM_SUPER_SHOTGUN) {
#if defined(DOOM_MODE_RUNTIME)
        if (weapon!=DOOM_ROCKET && weapon!=DOOM_PLASMA && weapon!=DOOM_BFG) return 0;
        damage_type=weapon==DOOM_PLASMA ? 2 : 3;
#else
        return 0;
#endif
    }
    uint32_t encoded=static_cast<uint32_t>(handle);
    if (!encoded || encoded>static_cast<uint32_t>(INT32_MAX)+1u) return 0;
    int id=static_cast<int>(encoded-1u);
    void *gs=c->get_object(c->world_level,id);
    if (!gs || c->get_id(gs)!=id || c->get_level(gs)!=c->world_level) return 0;
    void *target=c->dynamic_cast_object(gs,0,c->game+0x1C952A8,c->game+0x1C95390,0);
    if (!target || target==c->world_player || static_cast<unsigned char *>(target)+40!=gs ||
        c->get_level(static_cast<unsigned char *>(c->world_player)+40)!=c->world_level) return 0;
    if (damage_type!=2 || weapon==DOOM_PLASMA) {
        void *actor=c->dynamic_cast_object(target,0,c->game+0x1C95390,c->game+0x1CA9818,0);
        if (actor && doom_hostility_query(c->game,target,c->world_player)!=1) return 0;
    }
    float damage=static_cast<float>(amount)*HostDamagePerDoomUnit*c->world_damage_scale;
    if (!std::isfinite(damage) || damage<=0 || damage>100000.0f) return 0;
    /* Initial impulse calibration follows the stock grenade's 150:1500 ratio. */
    const float physics_damage=weapon==DOOM_ROCKET && splash ? damage*0.1f : 0.0f;
    int invalid_item=-1;
#if defined(DOOM_MODE_LAB)
    observe_before(c,target,id,weapon,damage,physics_damage);
    c->observation.mesh_element=c->world_last_mesh;
    c->observation.hit_bone=c->world_last_bone;
    c->observation.model_resolution=c->world_last_resolution;
    c->observation.model_reason=c->world_last_reason;
#endif
    c->give_damage(c->world_player,target,damage,physics_damage,
                   static_cast<unsigned char *>(c->world_player)+40,&c->world_last_point,
                   &c->world_last_direction,c->world_last_mesh,damage_type,&invalid_item,0.0f,0.0f,0);
#if defined(DOOM_MODE_LAB)
    observe_after(c,c->world_level,target,id);
    queue_observation(c);
#endif
    c->world_last_target=0;
    return 1;
}
int world_damage(void *ctx, uint64_t target, int amount, DoomWeapon weapon) {
    __try { return damage_target(static_cast<DoomCombat *>(ctx),target,amount,weapon); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
}
extern "C" int doom_combat_world_bind(DoomCombat *c, void *player, void *level,
                                      uint32_t generation, float damage_scale, DoomWorldHost *host) {
    if (!c || !player || !level || !host || !std::isfinite(damage_scale) ||
        damage_scale<=0 || damage_scale>1000.0f) return 0;
    if (c->world_generation!=generation) c->impact_head=c->impact_count=0;
    c->world_player=player; c->world_level=level; c->world_generation=generation;
    c->world_damage_scale=damage_scale; c->world_last_target=0;
    c->world_trace_budget=128;
    c->actor_cache_valid=false; c->actor_cache_count=0;
    if (!++c->world_epoch) ++c->world_epoch;
    std::memset(host,0,sizeof(*host));
    host->context=c; host->sweep=world_sweep; host->aim=world_aim; host->damage=world_damage;
    host->ground=world_ground;
    if (c->find_objects && c->native_free && c->object_position && c->object_bounds) {
        host->nearby=world_nearby; host->visible=world_visible;
    }
    return 1;
}
extern "C" void doom_combat_world_unbind(DoomCombat *c) {
    if (!c) return;
    doom_decals_close(c->decals); c->decals=nullptr;
    c->world_player=nullptr; c->world_level=nullptr; c->world_last_target=0;
    c->actor_cache_valid=false; c->actor_cache_count=0;
}
extern "C" int doom_combat_player_bounds(DoomCombat *c, void *player, void *level,
                                           DoomVec3 *center, float *radius, float *height) {
    if (!c || !player || !level || !center || !radius || !height ||
        c->world_player!=player || c->world_level!=level || !c->object_bounds || !c->get_level)
        return 0;
    __try {
        auto *actor=static_cast<unsigned char *>(player);
        if (c->get_level(actor+40)!=level) return 0;
        const float *native=c->object_bounds(actor+24);
        if (!native) return 0;
        float bounds[6]; std::memcpy(bounds,native,sizeof(bounds));
        for (float value : bounds) if (!std::isfinite(value)) return 0;
        for (unsigned i=0;i<3;++i) if (bounds[i]>=bounds[i+3]) return 0;
        const float scale=DOOM_WORLD_UNITS_PER_METRE;
        const float w=(bounds[3]-bounds[0])*0.5f*scale;
        const float d=(bounds[5]-bounds[2])*0.5f*scale;
        const float h=(bounds[4]-bounds[1])*scale;
        DoomVec3 midpoint{(bounds[0]+bounds[3])*0.5f*scale,
                          (bounds[2]+bounds[5])*0.5f*scale,
                          (bounds[1]+bounds[4])*0.5f*scale};
        if (!std::isfinite(midpoint.x) || !std::isfinite(midpoint.y) || !std::isfinite(midpoint.z) ||
            !std::isfinite(w) || !std::isfinite(d) || !std::isfinite(h) ||
            w<=0.0f || d<=0.0f || h<=0.0f || w>65536.0f || d>65536.0f || h>65536.0f ||
            c->world_player!=player || c->world_level!=level || c->get_level(actor+40)!=level)
            return 0;
        *center=midpoint; *radius=std::max(w,d); *height=h;
        return 1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
