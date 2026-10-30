#include "doom_combat.h"
#include "doom_hostility.h"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace {
struct Vec3 { float x, y, z; };
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
    void *world_player = nullptr, *world_level = nullptr;
    uint32_t world_generation = 0, world_epoch = 0;
    unsigned world_trace_budget = 0;
    uint64_t world_last_target = 0;
    Vec3 world_last_point{}, world_last_direction{};
    int world_last_mesh = -1;
    float world_damage_scale = 1.0f;
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
    void *find_address=reinterpret_cast<void *>(GetProcAddress(engine,"?FindObjectsInExtents@ILevel@@QEAA_NPEAV?$vector@PEAVIControlObject@@@ttl@@AEBVaabb@@PEBVCRTTI@@_NPEAH@Z"));
    static const unsigned char find_prefix[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x4c};
    HMODULE native_crt=GetModuleHandleW(L"MSVCR100.dll");
    c->native_free=native_crt ? reinterpret_cast<NativeFree>(GetProcAddress(native_crt,"free")) : nullptr;
    if (find_address==reinterpret_cast<unsigned char *>(engine)+0x2D05B0 &&
        !std::memcmp(find_address,find_prefix,sizeof(find_prefix)))
        c->find_objects=reinterpret_cast<FindObjects>(find_address);
    if (!c->camera_position || !c->camera_forward || !c->camera_up || !c->get_level ||
        !c->get_id || !c->get_object || !c->raytrace) { delete c; return nullptr; }
    return c;
}
extern "C" void doom_combat_destroy(DoomCombat *combat) { delete combat; }
extern "C" const char *doom_combat_last_reason(const DoomCombat *combat) {
    return combat ? combat->last_reason : "combat_missing";
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
    if (!c->raytrace(actor+40,0x27,collision.data(),&pos,&end,0,actor+24,0,0,4044024)) {
        c->last_reason="ray_missed";
        trace_hitscan("Doom hitscan: ray missed\n");
        return 1;
    }
    void *hit=nullptr; std::memcpy(&hit,collision.data()+24,sizeof(hit));
    std::memcpy(&mesh,collision.data()+32,sizeof(mesh));
    if (!hit) { c->last_reason="world_collision"; trace_hitscan("Doom hitscan: world collision\n"); return 1; }
    if (!valid(end)) { c->last_reason="impact_position_invalid"; return 1; }
    void *target=c->dynamic_cast_object(hit,0,c->game+0x1C952F0,c->game+0x1C95390,0);
    if (!target || target==player) { c->last_reason="target_cast_or_self"; trace_hitscan("Doom hitscan: no target object\n"); return 1; }
    auto *target_gs=static_cast<unsigned char *>(target)+40;
    if (c->get_level(actor+40)!=level || c->get_level(target_gs)!=level) {
        c->last_reason="target_level_mismatch"; trace_hitscan("Doom hitscan: target level mismatch\n"); return 0;
    }
    int id=c->get_id(target_gs);
    if (id<0 || c->get_object(level,id)!=target_gs) {
        c->last_reason="target_id_mismatch"; trace_hitscan("Doom hitscan: target ID mismatch\n"); return 0;
    }
    int invalid_item=-1;
    const float damage=static_cast<float>(event->damage)*scale;
    if (!std::isfinite(damage) || damage<=0 || damage>100000.0f) { c->last_reason="damage_invalid"; return 0; }
    /* Player engine EDamageType: cut=1, bullet=2, punch1=12. */
    const int damage_type=event->weapon==DOOM_FIST ? 12 :
                          event->weapon==DOOM_CHAINSAW ? 1 : 2;
    c->give_damage(player,target,damage,0.0f,actor+40,&end,&direction,mesh,damage_type,
                   &invalid_item,0.0f,0.0f,0);
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
int trace_segment(DoomCombat *c, DoomVec3 from, DoomVec3 to, DoomTrace *trace) {
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
    alignas(16) std::array<unsigned char,256> collision{};
    int mesh=-1; std::memcpy(collision.data()+32,&mesh,sizeof(mesh));
    auto *actor=static_cast<unsigned char *>(c->world_player);
    if (c->get_level(actor+40)!=c->world_level) return 0;
    bool hit=c->raytrace(actor+40,0x27,collision.data(),&start,&end,0,actor+24,0,0,4044024)!=0;
    if (!valid(end) || c->get_level(actor+40)!=c->world_level) return 0;
    trace->hit=hit ? 1 : 0;
    trace->point=world_position(end);
    if (!hit) return 1;
    void *control=nullptr; std::memcpy(&control,collision.data()+24,sizeof(control));
    if (!control) return 1;
    void *target=c->dynamic_cast_object(control,0,c->game+0x1C952F0,c->game+0x1C95390,0);
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
#if defined(DOOM_MODE_LAB) && defined(_DEBUG) && !defined(NDEBUG)
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
    uint64_t best_target=0; Vec3 best_point{},best_direction{}; int best_mesh=-1;
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
        }
    }
    if (!found) { std::memset(trace,0,sizeof(*trace)); trace->point=to; return 1; }
    *trace=best; c->world_last_target=best_target; c->world_last_point=best_point;
    c->world_last_direction=best_direction; c->world_last_mesh=best_mesh;
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
    *center={position.x+(bounds[0]+bounds[3])*0.5f,
             position.y+(bounds[1]+bounds[4])*0.5f,
             position.z+(bounds[2]+bounds[5])*0.5f};
    float x=(bounds[3]-bounds[0])*0.5f, z=(bounds[5]-bounds[2])*0.5f;
    *radius=(x>z ? x : z)*DOOM_WORLD_UNITS_PER_METRE;
    return valid(*center) && std::isfinite(*radius) && *radius>=0 && *radius<=65536.0f;
}
int collect_nearby(DoomCombat *c, DoomVec3 origin, float radius,
                   DoomTarget *out, unsigned capacity, unsigned *count) {
    NativeVector matches{nullptr,0,0,31};
    int success=0;
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
        for (uint32_t i=0; i<matches.size && *count<capacity; ++i) {
            void *control=matches.data[i];
            if (!control) continue;
            void *actor=c->dynamic_cast_object(control,0,c->game+0x1C952F0,c->game+0x1CA9818,0);
            void *target=c->dynamic_cast_object(control,0,c->game+0x1C952F0,c->game+0x1C95390,0);
            if (!actor || !target || target==c->world_player) continue;
            auto *gs=static_cast<unsigned char *>(target)+40;
            if (c->get_level(gs)!=c->world_level) continue;
            int id=c->get_id(gs);
            if (id<0 || c->get_object(c->world_level,id)!=gs) continue;
            Vec3 object_center{}; float object_radius=0;
            if (!object_geometry(c,target,&object_center,&object_radius)) continue;
            DoomTarget &item=out[*count];
            item.handle=(static_cast<uint64_t>(c->world_epoch)<<32) |
                        (static_cast<uint64_t>(static_cast<uint32_t>(id))+1u);
            item.position=world_position(object_center); item.radius=object_radius;
            ++*count;
        }
        if (c->get_level(player+40)==c->world_level) success=1;
    } __finally {
        if (matches.data) c->native_free(matches.data);
    }
    return success;
}
int world_nearby(void *ctx, DoomVec3 origin, float radius,
                 DoomTarget *out, unsigned capacity, unsigned *count) {
    __try { return collect_nearby(static_cast<DoomCombat *>(ctx),origin,radius,out,capacity,count); }
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
    if (!trace_segment(c,from,world_position(center),&trace)) return -1;
    if (!trace.hit) {
        Vec3 start=engine_position(from);
        c->world_last_target=handle; c->world_last_point=center;
        c->world_last_direction=unit({center.x-start.x,center.y-start.y,center.z-start.z});
        c->world_last_mesh=-1;
        return 1;
    }
    return trace.target==handle ? 1 : 0;
}
int world_visible(void *ctx, DoomVec3 from, uint64_t target) {
    __try { return visible_target(static_cast<DoomCombat *>(ctx),from,target); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int damage_target(DoomCombat *c, uint64_t handle, int amount, DoomWeapon weapon) {
    if (!c || !c->world_player || !c->world_level || !handle ||
        handle!=c->world_last_target || static_cast<uint32_t>(handle>>32)!=c->world_epoch ||
        amount<=0 || amount>100000) return 0;
    int damage_type=2;
    if (weapon!=DOOM_PISTOL && weapon!=DOOM_CHAINGUN &&
        weapon!=DOOM_SHOTGUN && weapon!=DOOM_SUPER_SHOTGUN) {
#if defined(DOOM_MODE_LAB) && defined(DOOM_LAB_HOSTILITY) && defined(_DEBUG) && !defined(NDEBUG)
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
    if (damage_type!=2 || weapon==DOOM_PLASMA)
        if (doom_hostility_query(c->game,target,c->world_player)!=1) return 0;
    float damage=static_cast<float>(amount)*c->world_damage_scale;
    if (!std::isfinite(damage) || damage<=0 || damage>100000.0f) return 0;
    int invalid_item=-1;
    c->give_damage(c->world_player,target,damage,0.0f,
                   static_cast<unsigned char *>(c->world_player)+40,&c->world_last_point,
                   &c->world_last_direction,c->world_last_mesh,damage_type,&invalid_item,0.0f,0.0f,0);
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
    c->world_player=player; c->world_level=level; c->world_generation=generation;
    c->world_damage_scale=damage_scale; c->world_last_target=0;
    c->world_trace_budget=128;
    if (!++c->world_epoch) ++c->world_epoch;
    std::memset(host,0,sizeof(*host));
    host->context=c; host->sweep=world_sweep; host->aim=world_aim; host->damage=world_damage;
    if (c->find_objects && c->native_free && c->object_position && c->object_bounds) {
        host->nearby=world_nearby; host->visible=world_visible;
    }
    return 1;
}
extern "C" void doom_combat_world_unbind(DoomCombat *c) {
    if (!c) return;
    c->world_player=nullptr; c->world_level=nullptr; c->world_last_target=0;
}
