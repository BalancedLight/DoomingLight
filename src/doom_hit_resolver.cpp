#include "doom_hit_resolver.h"
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

#if defined(DOOM_MODE_RUNTIME)
namespace {
struct Vec3 { float x, y, z; };
using Raytrace = unsigned char (__fastcall *)(void *, void *, const Vec3 *, Vec3 *, unsigned short, unsigned);
using GetBone = unsigned char (__fastcall *)(void *, int);
using GetMesh = int (__fastcall *)(void *, unsigned char);
using GetModelObjectFn = void *(__fastcall *)(void *);
bool finite(const Vec3 &v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
bool belongs_to(void *function, HMODULE first, HMODULE second) {
    HMODULE owner = nullptr;
    return function && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(function), &owner) &&
        (owner == first || owner == second);
}
}
struct DoomHitResolver {
    unsigned char *game, *engine;
    Raytrace raytrace;
    GetBone get_bone;
    GetMesh get_mesh;
};
extern "C" DoomHitResolver *doom_hit_resolver_create(void *game, void *engine) {
    if (!game || !engine || GetModuleHandleW(L"gamedll_x64_rwdi.dll") != game ||
        GetModuleHandleW(L"engine_x64_rwdi.dll") != engine) return nullptr;
    auto *g = static_cast<unsigned char *>(game), *e = static_cast<unsigned char *>(engine);
    static const unsigned char visual_prefix[] = {0x48,0x81,0xc1,0x40,0x03,0,0,0x48,0x8b,0x01,0x48,0xff};
    static const unsigned char ray_prefix[] = {0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48};
    static const unsigned char bone_prefix[] = {0x48,0x8b,0x49,0x08,0x48,0x85,0xc9,0x74,0x05,0xe9,0x42,0x8d};
    static const unsigned char mesh_prefix[] = {0x48,0x8b,0x49,0x08,0x48,0x85,0xc9,0x74,0x08,0x0f,0xb6,0xd2};
    __try {
        if (std::memcmp(g + 0x106b90, visual_prefix, sizeof visual_prefix) ||
            std::memcmp(e + 0x158f30, ray_prefix, sizeof ray_prefix) ||
            std::memcmp(e + 0x14e550, bone_prefix, sizeof bone_prefix) ||
            std::memcmp(e + 0x14e570, mesh_prefix, sizeof mesh_prefix)) return nullptr;
        if (reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(engine),
                "?RaytraceMe@IModelObject@@QEAA_NPEAUSCollision@@AEBVvec3@@AEAV3@GI@Z")) != e + 0x158f30 ||
            reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(engine),
                "?GetBoneIDFromMeshElem@IModelObject@@QEBAEH@Z")) != e + 0x14e550 ||
            reinterpret_cast<void *>(GetProcAddress(static_cast<HMODULE>(engine),
                "?GetMeshElemFromBoneID@IModelObject@@QEBAHE@Z")) != e + 0x14e570) return nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    auto *r = new (std::nothrow) DoomHitResolver{g,e,reinterpret_cast<Raytrace>(e+0x158f30),
        reinterpret_cast<GetBone>(e+0x14e550),reinterpret_cast<GetMesh>(e+0x14e570)};
    return r;
}
extern "C" DoomHitResolution doom_hit_resolver_trace(DoomHitResolver *r, void *actor,
                                                      const float start[3], const float end[3],
                                                      DoomResolvedHit *hit) {
    if (!hit) return DOOM_HIT_UNAVAILABLE;
    *hit = {-1, 255, {0,0,0}, DOOM_HIT_REASON_ARGUMENT};
    if (!r || !actor || !start || !end) return DOOM_HIT_UNAVAILABLE;
    hit->reason = DOOM_HIT_REASON_SEGMENT;
    const Vec3 from{start[0],start[1],start[2]}, to{end[0],end[1],end[2]};
    if (!finite(from) || !finite(to)) return DOOM_HIT_UNAVAILABLE;
    const Vec3 delta{to.x-from.x,to.y-from.y,to.z-from.z};
    const float length2=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
    if (!std::isfinite(length2) || length2 < 0.000001f || length2 > 2048.0f*2048.0f)
        return DOOM_HIT_UNAVAILABLE;
    __try {
        hit->reason = DOOM_HIT_REASON_ACTOR;
        auto **vtable=*reinterpret_cast<void ***>(actor);
        if (!vtable || vtable[46]!=r->game+0xe7c0f0 || vtable[194]!=r->game+0x106b90 ||
            !*reinterpret_cast<void **>(static_cast<unsigned char *>(actor)+8)) return DOOM_HIT_UNAVAILABLE;
        hit->reason = DOOM_HIT_REASON_VISUAL_GETTER;
        void *visual=reinterpret_cast<GetModelObjectFn>(vtable[194])(actor);
        if (!visual) return DOOM_HIT_UNAVAILABLE;
        void *visual_interface=static_cast<unsigned char *>(visual)+64;
        auto **visual_table=*reinterpret_cast<void ***>(visual_interface);
        if (!visual_table || !belongs_to(visual_table[1],reinterpret_cast<HMODULE>(r->game),
                                         reinterpret_cast<HMODULE>(r->engine))) return DOOM_HIT_UNAVAILABLE;
        hit->reason = DOOM_HIT_REASON_VISUAL_MODEL;
        void *visual_model=reinterpret_cast<GetModelObjectFn>(visual_table[1])(visual_interface);
        if (!visual_model || !*reinterpret_cast<void **>(static_cast<unsigned char *>(visual_model)+8))
            return DOOM_HIT_UNAVAILABLE;
        alignas(16) unsigned char collision[128]{};
        int source_mesh=-1;
        std::memcpy(collision+32,&source_mesh,sizeof source_mesh);
        Vec3 point=to;
        if (!r->raytrace(actor,collision,&from,&point,0,0)) {
            hit->reason = DOOM_HIT_REASON_MODEL_MISS;
            return DOOM_HIT_MISS;
        }
        hit->reason = DOOM_HIT_REASON_SOURCE_MESH;
        std::memcpy(&source_mesh,collision+32,sizeof source_mesh);
        if (source_mesh<0 || !finite(point)) return DOOM_HIT_UNAVAILABLE;
        hit->reason = DOOM_HIT_REASON_BONE;
        const unsigned char bone=r->get_bone(actor,source_mesh);
        if (bone==255) return DOOM_HIT_UNAVAILABLE;
        hit->reason = DOOM_HIT_REASON_VISUAL_MESH;
        const int visual_mesh=r->get_mesh(visual_model,bone);
        if (visual_mesh<0 || r->get_bone(visual_model,visual_mesh)!=bone)
            return DOOM_HIT_UNAVAILABLE;
        hit->reason = DOOM_HIT_REASON_CHANGED;
        if (reinterpret_cast<GetModelObjectFn>(vtable[194])(actor)!=visual ||
            reinterpret_cast<GetModelObjectFn>(visual_table[1])(visual_interface)!=visual_model)
            return DOOM_HIT_UNAVAILABLE;
        hit->reason = DOOM_HIT_REASON_OFF_SEGMENT;
        const Vec3 offset{point.x-from.x,point.y-from.y,point.z-from.z};
        const float fraction=(offset.x*delta.x+offset.y*delta.y+offset.z*delta.z)/length2;
        if (!std::isfinite(fraction) || fraction < -0.001f || fraction > 1.001f) return DOOM_HIT_UNAVAILABLE;
        hit->mesh_element=visual_mesh; hit->bone=bone;
        hit->point[0]=point.x; hit->point[1]=point.y; hit->point[2]=point.z;
        hit->reason = DOOM_HIT_REASON_NONE;
        return DOOM_HIT_RESOLVED;
    } __except(EXCEPTION_EXECUTE_HANDLER) { hit->reason=DOOM_HIT_REASON_EXCEPTION; return DOOM_HIT_UNAVAILABLE; }
}
extern "C" void doom_hit_resolver_destroy(DoomHitResolver *r) { delete r; }
#else
struct DoomHitResolver {};
extern "C" DoomHitResolver *doom_hit_resolver_create(void *, void *) { return nullptr; }
extern "C" DoomHitResolution doom_hit_resolver_trace(DoomHitResolver *, void *, const float *, const float *, DoomResolvedHit *) { return DOOM_HIT_UNAVAILABLE; }
extern "C" void doom_hit_resolver_destroy(DoomHitResolver *) {}
#endif
