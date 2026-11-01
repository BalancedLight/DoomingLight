#include "doom_decals.h"
#include <windows.h>
#include <cmath>
#include <new>

namespace {
using LoadTrace = void *(__fastcall *)(void *, const char *const *);
using ReleaseTrace = void (__fastcall *)(void *, void **);
using GetFX = void *(__fastcall *)(void *);
using AddTrace = void (__fastcall *)(void *, void *, const float *, const float *);
}

struct DoomDecals {
    unsigned char *engine;
    void *definition;
    ReleaseTrace release;
    GetFX get_fx;
};

extern "C" DoomDecals *doom_decals_open(void *engine) {
    if (!engine) return nullptr;
    auto *base = static_cast<unsigned char *>(engine);
    auto get_fx = reinterpret_cast<GetFX>(GetProcAddress(static_cast<HMODULE>(engine),
                                                         "?GetFXManager@ILevel@@QEAAPEAVIFXManager@@XZ"));
    if (!get_fx) return nullptr;
    auto *d = new(std::nothrow) DoomDecals{base, nullptr,
        reinterpret_cast<ReleaseTrace>(base + 0x29A5A0), get_fx};
    if (!d) return nullptr;
    const char *name = "BulletTraceGround_trace.fx";
    __try {
        d->definition = reinterpret_cast<LoadTrace>(base + 0x2951D0)(nullptr, &name);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        delete d;
        return nullptr;
    }
    if (!d->definition) { delete d; return nullptr; }
    return d;
}

extern "C" int doom_decals_add(DoomDecals *d, void *level,
                               const float position[3], const float normal[3]) {
    if (!d || !level || !position || !normal) return 0;
    for (int i = 0; i < 3; ++i)
        if (!std::isfinite(position[i]) || !std::isfinite(normal[i])) return 0;
    float len2 = normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2];
    if (!std::isfinite(len2) || len2 < 0.25f || len2 > 2.25f) return 0;
    __try {
        void *fx = d->get_fx(level);
        if (!fx) return 0;
        void **vtable = *reinterpret_cast<void ***>(fx);
        if (!vtable || vtable[48] != d->engine + 0x1EB200) return 0;
        reinterpret_cast<AddTrace>(vtable[48])(fx, d->definition, position, normal);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

extern "C" void doom_decals_close(DoomDecals *d) {
    if (!d) return;
    __try { if (d->definition) d->release(nullptr, &d->definition); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    delete d;
}
