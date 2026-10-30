/* Native pickup presentation bridge. GPL-2.0-or-later. */
#include "doom_native_pickup.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cstring>
#include <new>

struct DoomNativePickup {
    unsigned char *game;
    DoomNativePickupRecord pending[16];
    unsigned count;
    bool enabled, accepting, created, published;
};
namespace {
using Format = unsigned char (__fastcall *)(void *, const void *);
struct NativeText { const wchar_t *data; unsigned length, capacity; };
std::atomic<DoomNativePickup *> current{nullptr};
SRWLOCK lock = SRWLOCK_INIT;
Format original = nullptr;
DoomNativePickup *retained = nullptr;
bool claimed = false;

bool copy_text(const void *result, DoomNativePickupRecord *out) {
    __try {
        const auto *bytes = static_cast<const unsigned char *>(result);
        const auto *text = reinterpret_cast<const NativeText *>(bytes + 24);
        const char *icon = *reinterpret_cast<const char *const *>(bytes + 8);
        unsigned icon_length = *reinterpret_cast<const unsigned *>(bytes + 16);
        bool cash = icon && icon_length == 10 && std::memcmp(icon,"money_icon",10)==0;
        if (!text->data || !text->length || text->length > 32 || text->length > text->capacity) return false;
        static const char prefix[] = "You picked up ";
        unsigned offset = static_cast<unsigned>(sizeof(prefix) - 1);
        std::memcpy(out->text, prefix, offset);
        if (cash) out->text[offset++]='$';
        for (unsigned i=0; i<text->length; ++i) {
            wchar_t c=text->data[i];
            if (c<32 || (c>95 && (c<97 || c>122))) return false;
            out->text[i+offset]=static_cast<char>(c);
        }
        out->text[text->length+offset]=0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned char __fastcall format_hook(void *result, const void *item) {
    unsigned char formatted=original(result,item);
    if (!formatted || !TryAcquireSRWLockExclusive(&lock)) return formatted;
    DoomNativePickup *p=current.load(std::memory_order_acquire);
    if (p && p->enabled && p->accepting && p->count < 16 && copy_text(result,&p->pending[p->count])) {
        ++p->count;
        formatted=0; /* The caller frees the formatted data on either path. */
    }
    ReleaseSRWLockExclusive(&lock);
    return formatted;
}
}
extern "C" DoomNativePickup *doom_native_pickup_create(void *module) {
    auto *game=static_cast<unsigned char *>(module);
    static const unsigned char prefix[]={0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x55};
    if (!game || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=module ||
        std::memcmp(game+0x4f4710,prefix,sizeof(prefix))) return nullptr;
    AcquireSRWLockExclusive(&lock);
    if (claimed || (retained && retained->game!=game)) { ReleaseSRWLockExclusive(&lock); return nullptr; }
    auto *p=retained;
    if (!p) {
        p=new (std::nothrow) DoomNativePickup{};
        if (p) { p->game=game; retained=p; }
    }
    if (p) claimed=true;
    ReleaseSRWLockExclusive(&lock);
    return p;
}
extern "C" int doom_native_pickup_start(DoomNativePickup *p) {
    if (!p) return 0;
    AcquireSRWLockExclusive(&lock);
    DoomNativePickup *other=current.load(std::memory_order_acquire);
    if (other && other!=p) { ReleaseSRWLockExclusive(&lock); return 0; }
    if (p->enabled) { ReleaseSRWLockExclusive(&lock); return 1; }
    MH_STATUS status=MH_Initialize();
    if (status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) { ReleaseSRWLockExclusive(&lock); return 0; }
    if (!p->created) {
        status=MH_CreateHook(p->game+0x4f4710,reinterpret_cast<void *>(&format_hook),reinterpret_cast<void **>(&original));
        if (status!=MH_OK) { ReleaseSRWLockExclusive(&lock); return 0; }
        p->created=true;
    }
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&format_hook),&self)) { ReleaseSRWLockExclusive(&lock); return 0; }
    p->published=true;
    current.store(p,std::memory_order_release);
    status=MH_EnableHook(p->game+0x4f4710);
    if (status!=MH_OK && status!=MH_ERROR_ENABLED) { current.store(nullptr); ReleaseSRWLockExclusive(&lock); return 0; }
    p->enabled=true;
    ReleaseSRWLockExclusive(&lock);
    return 1;
}
extern "C" void doom_native_pickup_accept(DoomNativePickup *p, int accepting) {
    if (!p) return;
    AcquireSRWLockExclusive(&lock);
    p->accepting=accepting!=0;
    ReleaseSRWLockExclusive(&lock);
}
extern "C" unsigned doom_native_pickup_drain(DoomNativePickup *p, DoomNativePickupRecord *out, unsigned capacity) {
    if (!p || !out || !capacity || !TryAcquireSRWLockExclusive(&lock)) return 0;
    unsigned n=p->count<capacity ? p->count : capacity;
    for (unsigned i=0;i<n;++i) out[i]=p->pending[i];
    for (unsigned i=n;i<p->count;++i) p->pending[i-n]=p->pending[i];
    p->count-=n;
    ReleaseSRWLockExclusive(&lock);
    return n;
}
extern "C" int doom_native_pickup_stop(DoomNativePickup *p) {
    if (!p) return 1;
    AcquireSRWLockExclusive(&lock);
    p->enabled=false; p->accepting=false;
    p->count=0;
    if (p->created) {
        MH_STATUS status=MH_DisableHook(p->game+0x4f4710);
        if (status!=MH_OK && status!=MH_ERROR_DISABLED) { ReleaseSRWLockExclusive(&lock); return 0; }
    }
    DoomNativePickup *expected=p;
    current.compare_exchange_strong(expected,nullptr);
    ReleaseSRWLockExclusive(&lock);
    return 1;
}
extern "C" void doom_native_pickup_destroy(DoomNativePickup *p) {
    if (!p || !doom_native_pickup_stop(p)) return;
    AcquireSRWLockExclusive(&lock);
    claimed=false;
    if (!p->published) { if (p->created) (void)MH_RemoveHook(p->game+0x4f4710); retained=nullptr; delete p; }
    ReleaseSRWLockExclusive(&lock);
}
