#include "doom_chat.h"
#include <cstring>

#if defined(DOOM_MODE_RUNTIME)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <MinHook.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <mutex>

namespace {
struct WideString { const wchar_t *data; unsigned size, capacity; };
struct NativeChat { void *component, *edit, *messages; bool context, open; unsigned channel; };
using DisplayMessage = uintptr_t (__fastcall *)(void *, const void *);
using SelectRow = void *(__fastcall *)(void *);
using GetLevel = void *(__fastcall *)(void *);
using GetOpacity = float (__fastcall *)(void *);
using SetOpacity = void (__fastcall *)(void *, float);
using GetOwner = void *(__fastcall *)(void *);
using GetScreenId = int (__fastcall *)(void *);
using FindScreen = void *(__fastcall *)(void *, int);
unsigned char *game=nullptr;
void *owned_level=nullptr;
uint32_t owned_generation=0;
std::recursive_mutex state_mutex;
DisplayMessage original_display=nullptr;
SelectRow original_select=nullptr;
GetLevel get_level=nullptr;
GetOpacity get_opacity=nullptr;
SetOpacity set_opacity=nullptr;
GetOwner get_owner=nullptr;
GetScreenId get_screen_id=nullptr;
FindScreen find_screen=nullptr;
bool display_created=false, select_created=false;
std::atomic<bool> enabled{false};
std::atomic<bool> presentation_ready{false};
std::atomic<unsigned> queue_epoch{0};
struct Hidden { void *element=nullptr; float opacity=1.f; };
Hidden input_hidden;
struct MessageRow { void *element=nullptr; Hidden hidden; bool chat=false; };
MessageRow rows[64];
thread_local void *drawing_messages=nullptr;
thread_local bool drawing_chat=false;
char queued[16][DOOM_CHAT_TEXT_CAPACITY]{};
unsigned queue_begin=0, queue_count=0, pending_notifications=0;
char history[DOOM_CHAT_HISTORY][DOOM_CHAT_TEXT_CAPACITY]{};
unsigned history_count=0;
ULONGLONG last_message=0;

template<class T> T field(const void *base, size_t offset) {
    return *reinterpret_cast<const T *>(static_cast<const unsigned char *>(base)+offset);
}
bool owned_widget(void *element) {
    if (!element) return false;
    void *owner=get_owner(element);
    if (!owner) return false;
    const int id=get_screen_id(owner);
    void *root=id>=0 ? find_screen(owned_level,id) : nullptr;
    return root && get_owner(root)==owner;
}
bool native_chat(NativeChat *out) {
    if (!out || !enabled.load(std::memory_order_acquire) || !game || !owned_level || !owned_generation) return false;
    *out={};
    __try {
        void *g=field<void *>(game,0x1d07f88);
        if (!g || field<void *>(g,0)!=game+0x157d5f8) return false;
        void *session=field<void *>(g,1288);
        if (!session) return false;
        void **table=field<void **>(session,0);
        if (!table || table[244]!=game+0x6ad140) return false;
        void *level_di=reinterpret_cast<GetLevel>(table[244])(session);
        if (!level_di || field<void *>(level_di,16)!=game+0x15822e8 ||
            get_level(static_cast<unsigned char *>(level_di)+16)!=owned_level) return false;
        out->context=true;
        void *manager=field<void *>(game,0x1d07fd0);
        void *chat=manager ? field<void *>(manager,632) : nullptr;
        out->component=chat;
        out->messages=manager ? field<void *>(manager,624) : nullptr;
        if (!chat || field<void *>(chat,0)!=game+0x15b9618) return false;
        out->edit=field<void *>(chat,96);
        out->open=field<unsigned char>(chat,108)!=0 && (field<unsigned char>(level_di,3428)&1u)!=0;
        out->channel=field<unsigned>(chat,104);
        return out->edit && out->messages && out->channel<=1 &&
            field<void *>(out->messages,0)==game+0x15cce18 && owned_widget(chat) &&
            owned_widget(out->messages) && get_owner(out->edit)==get_owner(chat);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool copy_wide(const WideString *source, wchar_t *out, unsigned capacity) {
    if (!source || !out || capacity<2) return false;
    __try {
        const unsigned count=source->size;
        if (count>=capacity || count>source->capacity || (count && !source->data)) return false;
        if (count) std::memcpy(out,source->data,count*sizeof(wchar_t));
        out[count]=0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void utf8(const wchar_t *source, char *out, unsigned capacity) {
    if (!WideCharToMultiByte(CP_UTF8,0,source,-1,out,static_cast<int>(capacity),nullptr,nullptr)) out[0]=0;
}
void enqueue(const char *line, unsigned epoch) {
    if (!enabled.load(std::memory_order_acquire) || queue_epoch.load(std::memory_order_acquire)!=epoch) return;
    if (queue_count==16) { queue_begin=(queue_begin+1)%16; --queue_count; }
    std::memcpy(queued[(queue_begin+queue_count)%16],line,DOOM_CHAT_TEXT_CAPACITY);
    ++queue_count;
}
bool caption(const void *record, char *out) {
    wchar_t plain[320]{};
    unsigned used=0;
    __try {
        const auto *text=static_cast<const WideString *>(record);
        if (!text || !text->data || !text->size || text->size>4096 || text->size>text->capacity) return false;
        for (unsigned i=0;i<text->size;++i) {
            wchar_t ch=text->data[i];
            if (ch==L'<' && i+1<text->size && (text->data[i+1]==L'/' ||
                (text->data[i+1]>=L'A' && text->data[i+1]<=L'Z') ||
                (text->data[i+1]>=L'a' && text->data[i+1]<=L'z'))) {
                while (i<text->size && text->data[i]!=L'>') ++i;
                if (i==text->size) return false;
                continue;
            }
            if (ch==L'&') {
                bool decoded=false;
                static const wchar_t *const names[]={L"amp;",L"lt;",L"gt;",L"quot;",L"apos;",L"nbsp;"};
                static const wchar_t values[]={L'&',L'<',L'>',L'"',L'\'',L' '};
                for (unsigned n=0;n<6;++n) {
                    const size_t length=std::wcslen(names[n]);
                    if (i+1+length<=text->size && !std::wmemcmp(text->data+i+1,names[n],length)) {
                        ch=values[n]; i+=static_cast<unsigned>(length); decoded=true; break;
                    }
                }
                if (!decoded) for (unsigned n=1;n<=12 && i+n<text->size;++n) {
                    if (text->data[i+n]==L';') return false;
                    if (text->data[i+n]<=L' ') break;
                }
            }
            if (ch>126 || ch==96 || ch>=123) return false;
            if (used+1>=sizeof(plain)/sizeof(plain[0])) return false;
            plain[used++]=ch;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    utf8(plain,out,DOOM_CHAT_TEXT_CAPACITY);
    return used && out[0];
}
void opacity(Hidden &saved, void *element, bool suppress) {
    if (saved.element!=element) saved={};
    if (!element) return;
    const float value=get_opacity(element);
    if (!std::isfinite(value) || value<0.f || value>1.f) return;
    if (suppress) {
        if (!saved.element || value!=0.f) saved={element,value};
        set_opacity(element,0.f);
    } else if (saved.element) {
        if (value==0.f) set_opacity(element,saved.opacity);
        saved={};
    }
}
void *select_row(void *messages) {
    void *row=original_select(messages);
    if (drawing_messages!=messages || !row) return row;
    __try {
        if (field<void *>(row,0)!=game+0x15cc7f8 || !owned_widget(row)) return row;
        MessageRow *slot=nullptr;
        for (auto &entry:rows) {
            if (entry.element==row) { slot=&entry; break; }
            if (!entry.element && !slot) slot=&entry;
        }
        if (slot) {
            opacity(slot->hidden,row,false);
            slot->element=row;
            slot->chat=drawing_chat;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return row;
}
void * __fastcall select_hook(void *messages) {
    std::lock_guard<std::recursive_mutex> guard(state_mutex);
    return select_row(messages);
}
uintptr_t display_message(void *messages, const void *record) {
    bool chat=false, received=false;
    char line[DOOM_CHAT_TEXT_CAPACITY]{};
    NativeChat state{};
    __try {
        received=native_chat(&state) && state.messages==messages &&
            record && field<int>(record,32)==2;
        chat=received && caption(record,line);
    } __except(EXCEPTION_EXECUTE_HANDLER) { chat=false; }
    if (received && pending_notifications<16) ++pending_notifications;
    if (chat) enqueue(line,queue_epoch.load(std::memory_order_acquire));
    /* UIUpdate consumes the record after this presentation call. Native chat
       delivery and the separate history have already completed. */
    if (chat && presentation_ready.load(std::memory_order_acquire)) return 0;
    void *previous=drawing_messages;
    const bool previous_chat=drawing_chat;
    drawing_messages=messages; drawing_chat=chat;
    const uintptr_t result=original_display(messages,record);
    drawing_messages=previous; drawing_chat=previous_chat;
    return result;
}
uintptr_t __fastcall display_hook(void *messages, const void *record) {
    std::lock_guard<std::recursive_mutex> guard(state_mutex);
    return display_message(messages,record);
}
void native_presentation(bool suppress) {
    NativeChat state{};
    const bool valid=native_chat(&state);
    __try {
        if (!state.context) { input_hidden={}; std::memset(rows,0,sizeof(rows)); return; }
        if (state.component && owned_widget(state.component))
            opacity(input_hidden,state.component,valid && suppress && state.open);
        else input_hidden={};
        void *members[64]{};
        unsigned count=0;
        if (state.messages && field<void *>(state.messages,0)==game+0x15cce18 && owned_widget(state.messages)) {
            void *node=field<void *>(state.messages,88);
            while (node && count<64) {
                members[count++]=field<void *>(static_cast<unsigned char *>(node)-8,0);
                node=field<void *>(node,0);
            }
        }
        for (auto &entry:rows) if (entry.element) {
            bool member=false;
            for (unsigned i=0;i<count;++i) if (members[i]==entry.element) { member=true; break; }
            if (!member || field<void *>(entry.element,0)!=game+0x15cc7f8 || !owned_widget(entry.element)) {
                entry={}; continue;
            }
            opacity(entry.hidden,entry.element,valid && suppress && entry.chat);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { input_hidden={}; std::memset(rows,0,sizeof(rows)); }
}
}

extern "C" int doom_chat_start(void *module, void *engine_module, void *level, uint32_t generation, int supported) {
    std::lock_guard<std::recursive_mutex> guard(state_mutex);
    auto *base=static_cast<unsigned char *>(module);
    auto engine=static_cast<HMODULE>(engine_module);
    static const unsigned char display_prefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x40};
    static const unsigned char select_prefix[]={0x48,0x89,0x5c,0x24,0x10,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0x41,0x60};
    if (!supported || !base || !engine || !level || !generation ||
        GetModuleHandleW(L"gamedll_x64_rwdi.dll")!=module || GetModuleHandleW(L"engine_x64_rwdi.dll")!=engine ||
        std::memcmp(base+0x4fbff0,display_prefix,sizeof(display_prefix)) ||
        std::memcmp(base+0x4fbe00,select_prefix,sizeof(select_prefix)) || (game && game!=base)) return 0;
    get_level=reinterpret_cast<GetLevel>(GetProcAddress(engine,"?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ"));
    get_opacity=reinterpret_cast<GetOpacity>(GetProcAddress(engine,"?GetOpacity@IUIElement@@UEBAMXZ"));
    set_opacity=reinterpret_cast<SetOpacity>(GetProcAddress(engine,"?SetOpacity@IUIElement@@UEAAXM@Z"));
    get_owner=reinterpret_cast<GetOwner>(GetProcAddress(engine,"?GetOwnerScreen@IUIElement@@QEBAPEAVIUIScreen@@XZ"));
    get_screen_id=reinterpret_cast<GetScreenId>(GetProcAddress(engine,"?GetScreenId@IUIScreen@@QEBAHXZ"));
    find_screen=reinterpret_cast<FindScreen>(GetProcAddress(engine,"?FindUiScreen@ILevel@@QEBAPEAVIUIElement@@H@Z"));
    if (!get_level || !get_owner || !get_screen_id || !find_screen ||
        reinterpret_cast<void *>(get_opacity)!=static_cast<unsigned char *>(engine_module)+0x5dff70 ||
        reinterpret_cast<void *>(set_opacity)!=static_cast<unsigned char *>(engine_module)+0x5e69c0) return 0;
    HMODULE self=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&display_hook),&self)) return 0;
    MH_STATUS status=MH_Initialize();
    if (status!=MH_OK && status!=MH_ERROR_ALREADY_INITIALIZED) return 0;
    game=base;
    if (!display_created) {
        if (MH_CreateHook(base+0x4fbff0,reinterpret_cast<void *>(&display_hook),reinterpret_cast<void **>(&original_display))!=MH_OK) return 0;
        display_created=true;
    }
    if (!select_created) {
        if (MH_CreateHook(base+0x4fbe00,reinterpret_cast<void *>(&select_hook),reinterpret_cast<void **>(&original_select))!=MH_OK) return 0;
        select_created=true;
    }
    owned_level=level; owned_generation=generation;
    presentation_ready.store(false,std::memory_order_release);
    queue_epoch.fetch_add(1,std::memory_order_acq_rel);
    enabled.store(true,std::memory_order_release);
    const unsigned offsets[]={0x4fbff0,0x4fbe00};
    for (unsigned offset:offsets) {
        status=MH_EnableHook(base+offset);
        if (status!=MH_OK && status!=MH_ERROR_ENABLED) { doom_chat_stop(); return 0; }
    }
    return 1;
}
extern "C" int doom_chat_typing(void) {
    std::lock_guard<std::recursive_mutex> guard(state_mutex);
    NativeChat state{};
    return native_chat(&state) && state.open;
}
extern "C" void doom_chat_poll(DoomChatSnapshot *out) {
    std::lock_guard<std::recursive_mutex> guard(state_mutex);
    if (!out) return;
    *out={};
    NativeChat state{};
    if (!native_chat(&state)) {
        native_presentation(false);
        return;
    }
    out->available=1; out->open=state.open; out->channel=static_cast<int>(state.channel);
    wchar_t draft[251]{};
    if (out->open && copy_wide(reinterpret_cast<WideString *>(static_cast<unsigned char *>(state.edit)+160),draft,251))
        utf8(draft,out->draft,sizeof(out->draft));
    {
        out->notifications=pending_notifications;
        pending_notifications=0;
        while (queue_count) {
            if (history_count==DOOM_CHAT_HISTORY) {
                std::memmove(history,history+1,(DOOM_CHAT_HISTORY-1)*DOOM_CHAT_TEXT_CAPACITY);
                --history_count;
            }
            std::memcpy(history[history_count++],queued[queue_begin],DOOM_CHAT_TEXT_CAPACITY);
            queue_begin=(queue_begin+1)%16; --queue_count;
            last_message=GetTickCount64();
        }
    }
    if (out->open || (last_message && GetTickCount64()-last_message<7000)) {
        out->line_count=out->open ? history_count : (history_count ? 1u : 0u);
        for (unsigned i=0;i<out->line_count;++i)
            std::memcpy(out->lines[i],history[out->open ? i : history_count-1],DOOM_CHAT_TEXT_CAPACITY);
    }
}
extern "C" void doom_chat_presented(int ready) {
    std::lock_guard<std::recursive_mutex> guard(state_mutex);
    presentation_ready.store(ready!=0,std::memory_order_release);
    native_presentation(ready!=0);
}
extern "C" void doom_chat_stop(void) {
    std::lock_guard<std::recursive_mutex> guard(state_mutex);
    native_presentation(false);
    presentation_ready.store(false,std::memory_order_release);
    enabled.store(false,std::memory_order_release);
    queue_epoch.fetch_add(1,std::memory_order_acq_rel);
    if (display_created) (void)MH_DisableHook(game+0x4fbff0);
    if (select_created) (void)MH_DisableHook(game+0x4fbe00);
    owned_level=nullptr; owned_generation=0;
    queue_begin=queue_count=history_count=pending_notifications=0; last_message=0;
    input_hidden={}; std::memset(rows,0,sizeof(rows));
    std::memset(history,0,sizeof(history));
}
#else
extern "C" int doom_chat_start(void *,void *,void *,uint32_t,int) { return 0; }
extern "C" int doom_chat_typing(void) { return 0; }
extern "C" void doom_chat_poll(DoomChatSnapshot *out) { if (out) *out={}; }
extern "C" void doom_chat_presented(int) {}
extern "C" void doom_chat_stop(void) {}
#endif
