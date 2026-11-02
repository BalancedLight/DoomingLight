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
#include <intrin.h>

struct DoomNativePickup {
    unsigned char *game;
    DoomNativePickupRecord pending[16];
    unsigned count;
    bool enabled, accepting, created, wrapper_created, published;
};
namespace {
using Wrapper = void (__fastcall *)(void *, const void *);
using Format = unsigned char (__fastcall *)(void *, const void *);
struct NativeText { const wchar_t *data; unsigned length, capacity; };
std::atomic<DoomNativePickup *> current{nullptr};
SRWLOCK lock = SRWLOCK_INIT;
Format original = nullptr;
Wrapper original_wrapper = nullptr;
thread_local const void *pickup_item = nullptr;
DoomNativePickup *retained = nullptr;
bool claimed = false;

bool copy_text(const void *result, DoomNativePickupRecord *out) {
    __try {
        const auto *bytes = static_cast<const unsigned char *>(result);
        const auto *text = reinterpret_cast<const NativeText *>(bytes + 24);
        const char *icon = *reinterpret_cast<const char *const *>(bytes + 8);
        unsigned icon_length = *reinterpret_cast<const unsigned *>(bytes + 16);
        if (icon && icon_length == 16 && std::memcmp(icon,"collectable_icon",16)==0) {
            out->kind=DOOM_NATIVE_PICKUP_SECRET;
            std::memcpy(out->text,"A secret is revealed!",sizeof("A secret is revealed!"));
            return true;
        }
        out->kind=DOOM_NATIVE_PICKUP_NORMAL;
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
    if (!formatted || item!=pickup_item || !pickup_item || !TryAcquireSRWLockExclusive(&lock)) return formatted;
    DoomNativePickup *p=current.load(std::memory_order_acquire);
    if (p && p->enabled && p->accepting) {
        DoomNativePickupRecord record{};
        if (copy_text(result,&record)) {
            if (p->count==16 && record.kind==DOOM_NATIVE_PICKUP_SECRET) {
                unsigned i=0;
                while (i<p->count && p->pending[i].kind!=DOOM_NATIVE_PICKUP_NORMAL) ++i;
                if (i<p->count) {
                    for (unsigned j=i+1;j<p->count;++j) p->pending[j-1]=p->pending[j];
                    --p->count;
                }
            }
            if (p->count<16) {
                p->pending[p->count++]=record;
                formatted=0; /* The caller frees the formatted data on either path. */
            }
        }
    }
    ReleaseSRWLockExclusive(&lock);
    return formatted;
}
void __fastcall wrapper_hook(void *self, const void *item) {
    DoomNativePickup *p=current.load(std::memory_order_acquire);
    const void *previous=pickup_item;
    pickup_item=(p && _ReturnAddress()==p->game+0x4f46d6) ? item : nullptr;
    __try { original_wrapper(self,item); }
    __finally { pickup_item=previous; }
}
}
extern "C" DoomNativePickup *doom_native_pickup_create(void *module) {
    auto *game=static_cast<unsigned char *>(module);
    static const unsigned char prefix[]={0x48,0x89,0x54,0x24,0x10,0x48,0x89,0x4c,0x24,0x08,0x55,0x56,0x57,0x41,0x55};
    static const unsigned char wrapper[]={0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x56,0x48,0x83,0xec,0x60};
    static const unsigned char pickup_call[]={0xe8,0xca,0xf9,0xff,0xff};
    if (!game || GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=module ||
        std::memcmp(game+0x4f4710,prefix,sizeof(prefix)) ||
        std::memcmp(game+0x4f40a0,wrapper,sizeof(wrapper)) ||
        std::memcmp(game+0x4f46d1,pickup_call,sizeof(pickup_call))) return nullptr;
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
    if (!p->wrapper_created) {
        status=MH_CreateHook(p->game+0x4f40a0,reinterpret_cast<void *>(&wrapper_hook),reinterpret_cast<void **>(&original_wrapper));
        if (status!=MH_OK) { ReleaseSRWLockExclusive(&lock); return 0; }
        p->wrapper_created=true;
    }
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                            reinterpret_cast<LPCWSTR>(&format_hook),&self)) { ReleaseSRWLockExclusive(&lock); return 0; }
    p->published=true;
    current.store(p,std::memory_order_release);
    status=MH_QueueEnableHook(p->game+0x4f4710);
    if (status==MH_OK) status=MH_QueueEnableHook(p->game+0x4f40a0);
    if (status==MH_OK) status=MH_ApplyQueued();
    if (status!=MH_OK) { current.store(nullptr); ReleaseSRWLockExclusive(&lock); return 0; }
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
    if (p->wrapper_created) {
        MH_STATUS status=MH_DisableHook(p->game+0x4f40a0);
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
    if (!p->published) { if (p->created) (void)MH_RemoveHook(p->game+0x4f4710); if (p->wrapper_created) (void)MH_RemoveHook(p->game+0x4f40a0); retained=nullptr; delete p; }
    ReleaseSRWLockExclusive(&lock);
}
