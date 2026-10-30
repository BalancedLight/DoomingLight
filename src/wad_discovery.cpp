#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include "wad_discovery.h"
#include "doom_wad.h"
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;
static std::string utf8(const std::wstring &value) {
    if (value.empty()) return {};
    int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0,nullptr,nullptr);
    if (!n) return {};
    std::string result((size_t)n,'\0');
    if (!WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),(int)value.size(),&result[0],n,nullptr,nullptr)) return {};
    return result;
}
static std::wstring wide(const std::string &value) {
    if(value.empty()) return {};
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),nullptr,0);
    if(!n) return {};
    std::wstring result((size_t)n,L'\0');
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),(int)value.size(),&result[0],n)) return {};
    return result;
}
static bool valid(const std::string &path,int game) {
    DoomWad *wad=doom_wad_open(path.c_str()); if(!wad) return false;
    bool match=(doom_wad_is_doom2(wad) != 0)==(game==2); doom_wad_close(wad); return match;
}
static std::vector<std::string> tokens(const fs::path &file) {
    std::ifstream in(file,std::ios::binary); if(!in) return {};
    in.seekg(0,std::ios::end); auto length=in.tellg();
    if(length<0 || length>4*1024*1024) return {};
    in.seekg(0,std::ios::beg); if(!in) return {};
    std::string text((size_t)length,'\0');
    if(length && !in.read(&text[0],length)) return {};
    std::vector<std::string> result;
    for(size_t i=0;i<text.size();) {
        if(text[i++]!='"') continue;
        std::string value;
        while(i<text.size() && text[i]!='"') {
            if(text[i]=='\\' && i+1<text.size() && (text[i+1]=='\\' || text[i+1]=='"')) ++i;
            value.push_back(text[i++]);
        }
        if(i==text.size()) return {}; ++i; result.push_back(value);
    }
    return result;
}
static bool store(const std::string &path,char *out,size_t capacity) {
    if(path.size()+1>capacity) return false; std::memcpy(out,path.c_str(),path.size()+1); return true;
}
extern "C" int doom_find_iwad(int game,const char *manual_path,char *out,size_t capacity) {
    if(!out || !capacity || (game!=1 && game!=2)) return 0; out[0]='\0';
    if(manual_path && *manual_path) return valid(manual_path,game) && store(manual_path,out,capacity);
    wchar_t steam[32768]; DWORD bytes=sizeof(steam), type=0;
    if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,&type,steam,&bytes)!=ERROR_SUCCESS) return 0;
    std::vector<fs::path> libraries{fs::path(steam)};
    auto library_tokens=tokens(fs::path(steam)/L"steamapps"/L"libraryfolders.vdf");
    for(size_t i=0;i+1<library_tokens.size() && libraries.size()<64;++i) if(library_tokens[i]=="path") { auto p=wide(library_tokens[i+1]); if(!p.empty()) libraries.emplace_back(p); }
    size_t manifest_entries=0, walk_entries=0;
    for(const auto &library:libraries) {
        std::error_code ec; fs::path apps=library/L"steamapps";
        for(fs::directory_iterator it(apps,fs::directory_options::skip_permission_denied,ec), end; !ec && it!=end; it.increment(ec)) {
            if(++manifest_entries>20000) return 0;
            auto filename=it->path().filename().wstring();
            if(filename.rfind(L"appmanifest_",0)!=0 || it->path().extension()!=L".acf") continue;
            auto data=tokens(it->path()); std::string name,dir;
            for(size_t i=0;i+1<data.size();++i) { if(data[i]=="name") name=data[i+1]; if(data[i]=="installdir") dir=data[i+1]; }
            std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){ return (char)tolower(c); });
            auto relative=fs::path(wide(dir)); if(name.find("doom")==std::string::npos || relative.empty() || relative.has_parent_path() || relative.is_absolute()) continue;
            fs::path root=apps/L"common"/relative;
            DWORD root_attrs=GetFileAttributesW(root.c_str());
            if(root_attrs==INVALID_FILE_ATTRIBUTES || (root_attrs & FILE_ATTRIBUTE_REPARSE_POINT)) continue;
            std::error_code walk_error;
            fs::recursive_directory_iterator walk(root,fs::directory_options::skip_permission_denied,walk_error), walk_end;
            for(;!walk_error && walk!=walk_end;walk.increment(walk_error)) {
                if(++walk_entries>20000) return 0;
                DWORD attrs=GetFileAttributesW(walk->path().c_str());
                if(attrs==INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) { walk.disable_recursion_pending(); continue; }
                if(walk.depth()>=5 && walk->is_directory(walk_error)) walk.disable_recursion_pending();
                auto file=walk->path().filename().wstring(); std::transform(file.begin(),file.end(),file.begin(),::towlower);
                if(file!=(game==2?L"doom2.wad":L"doom.wad")) continue;
                auto candidate=utf8(walk->path().wstring()); if(valid(candidate,game) && store(candidate,out,capacity)) return 1;
            }
        }
    }
    return 0;
}
