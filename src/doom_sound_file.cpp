#include "doom_sound_file.h"
#include <windows.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {
constexpr uint64_t MaxFileBytes = 32u * 1024u * 1024u;
constexpr unsigned MaxPathChars = 32767u;
uint16_t u16(const unsigned char *p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}
uint32_t u32(const unsigned char *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
        (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
bool local_drive(const wchar_t *p) {
    if (!p || !((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')) ||
        p[1] != L':' || (p[2] != L'\\' && p[2] != L'/')) return false;
    wchar_t root[] = {p[0], L':', L'\\', 0};
    const UINT type = GetDriveTypeW(root);
    return type == DRIVE_FIXED || type == DRIVE_REMOVABLE || type == DRIVE_CDROM;
}
bool clean_path(std::vector<wchar_t> &path) {
    if (path.size() < 4 || !local_drive(path.data())) return false;
    for (size_t i = 3; i < path.size() && path[i]; ++i) {
        if (path[i] == L':') return false;
        if (path[i] == L'\\' || path[i] == L'/') {
            const wchar_t saved = path[i]; path[i] = 0;
            const DWORD attributes = GetFileAttributesW(path.data()); path[i] = saved;
            if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
        }
    }
    const DWORD attributes = GetFileAttributesW(path.data());
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}
bool local_handle(HANDLE file) {
    const DWORD needed = GetFinalPathNameByHandleW(file, nullptr, 0, FILE_NAME_NORMALIZED);
    if (!needed || needed > MaxPathChars) return false;
    std::vector<wchar_t> final_path(static_cast<size_t>(needed) + 1u);
    const DWORD written = GetFinalPathNameByHandleW(file, final_path.data(),
        static_cast<DWORD>(final_path.size()), FILE_NAME_NORMALIZED);
    if (!written || written >= final_path.size()) return false;
    return written >= 7 && final_path[0] == L'\\' && final_path[1] == L'\\' &&
        final_path[2] == L'?' && final_path[3] == L'\\' && local_drive(final_path.data() + 4);
}
bool parse_wav(const std::vector<unsigned char> &bytes, DoomSound *out) {
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) ||
        std::memcmp(bytes.data() + 8, "WAVE", 4)) return false;
    const uint64_t end64 = static_cast<uint64_t>(u32(bytes.data() + 4)) + 8u;
    if (end64 < 12 || end64 > bytes.size()) return false;
    const size_t end = static_cast<size_t>(end64);
    size_t pos = 12, data_pos = 0, data_size = 0;
    unsigned channels = 0, rate = 0, align = 0;
    bool got_fmt = false, got_data = false;
    while (pos < end) {
        if (end - pos < 8) return false;
        const unsigned char *header = bytes.data() + pos;
        const size_t size = u32(header + 4); pos += 8;
        if (size > end - pos || (size & 1u && size == end - pos)) return false;
        if (!std::memcmp(header, "fmt ", 4)) {
            if (got_fmt || size < 16 || u16(bytes.data() + pos) != 1) return false;
            channels = u16(bytes.data() + pos + 2); rate = u32(bytes.data() + pos + 4);
            const uint32_t byte_rate = u32(bytes.data() + pos + 8);
            align = u16(bytes.data() + pos + 12);
            if ((channels != 1 && channels != 2) || rate < 1000 || rate > 192000 ||
                u16(bytes.data() + pos + 14) != 16 || align != channels * 2 ||
                byte_rate != rate * align) return false;
            got_fmt = true;
        } else if (!std::memcmp(header, "data", 4)) {
            if (got_data) return false;
            data_pos = pos; data_size = size; got_data = true;
        }
        pos += size + (size & 1u);
    }
    if (!got_fmt || !got_data || !data_size || data_size % align) return false;
    const size_t frames = data_size / align;
    if (frames > static_cast<size_t>(rate) * 60u || frames > UINT32_MAX) return false;
    auto *samples = static_cast<int16_t *>(std::malloc(frames * sizeof(int16_t)));
    if (!samples) return false;
    for (size_t i = 0; i < frames; ++i) {
        const unsigned char *frame = bytes.data() + data_pos + i * align;
        const int first = static_cast<int16_t>(u16(frame));
        samples[i] = static_cast<int16_t>(channels == 1 ? first :
            (first + static_cast<int16_t>(u16(frame + 2))) / 2);
    }
    out->rate = rate; out->count = static_cast<uint32_t>(frames); out->samples = samples;
    return true;
}
}

extern "C" int doom_sound_file_load(const char *utf8_path, DoomSound *out) {
    if (!out) return 0;
    std::memset(out, 0, sizeof(*out));
    if (!utf8_path || !utf8_path[0]) return 0;
    try {
        const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path, -1, nullptr, 0);
        if (len <= 0 || static_cast<unsigned>(len) > MaxPathChars) return 0;
        std::vector<wchar_t> path(static_cast<size_t>(len));
        if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8_path, -1, path.data(), len) || !clean_path(path)) return 0;
        HANDLE file = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return 0;
        bool ok = false;
        try {
            LARGE_INTEGER size{};
            if (GetFileSizeEx(file, &size) && size.QuadPart >= 12 &&
                static_cast<uint64_t>(size.QuadPart) <= MaxFileBytes && local_handle(file)) {
                std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart));
                DWORD read = 0;
                if (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size())
                    ok = parse_wav(bytes, out);
            }
        } catch (...) { ok = false; }
        CloseHandle(file);
        return ok ? 1 : 0;
    } catch (...) { return 0; }
}
