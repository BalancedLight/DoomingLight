#define _CRT_SECURE_NO_WARNINGS
#include "dluma_api.h"
#include "doom_audio.h"
#include "doom_core.h"
#include "doom_engine.h"
#include "doom_gamepad.h"
#include "doom_hud.h"
#include "doom_music.h"
#include "doom_music_selection.h"
#include "doom_movement.h"
#include "doom_pickup_feedback.h"
#include "doom_synth.h"
#include "doom_sound_file.h"
#include "doom_wad.h"
#include "doom_world_display.h"
#include "doom_world_runtime.h"
#include "doom_vehicle_feedback.h"
#include "wad_discovery.h"
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <random>
#include <string>
#include <vector>

namespace {
constexpr uint64_t UpdateCallback = 1, ConfigCallback = 2;
std::string bundledFile(const wchar_t* filename) {
    HMODULE module=nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&bundledFile),&module)) return {};
    std::wstring path(32768,L'\0');
    const DWORD length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if (!length || length>=path.size()) return {};
    path.resize(length);
    if (path.compare(0,4,L"\\\\?\\")==0) path.erase(0,4);
    const auto slash=path.find_last_of(L"/\\");
    if (slash==std::wstring::npos) return {};
    path.resize(slash+1); path+=filename;
    const int bytes=WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,nullptr,0,nullptr,nullptr);
    if (bytes<=1) return {};
    std::string result(static_cast<size_t>(bytes),'\0');
    if (!WideCharToMultiByte(CP_UTF8,0,path.c_str(),-1,result.data(),bytes,nullptr,nullptr)) return {};
    result.pop_back(); return result;
}
DlumaStringView view(const char* value) { return {value, std::strlen(value)}; }
template<class T> T record() {
    T value{};
    value.struct_size = sizeof(value);
    value.struct_version = 1;
    return value;
}
template<class T> T resultRecord() {
    T value = record<T>();
    value.call = record<DlumaResult>();
    return value;
}
struct WadDeleter { void operator()(DoomWad* p) const { doom_wad_close(p); } };
struct HudDeleter { void operator()(DoomHud* p) const { doom_hud_destroy(p); } };
struct PcmDeleter { void operator()(int16_t* p) const { std::free(p); } };
using WadPtr = std::unique_ptr<DoomWad, WadDeleter>;
using HudPtr = std::unique_ptr<DoomHud, HudDeleter>;
std::vector<uint8_t> readSoundFont(const std::string& path) {
    if (path.empty()) return {};
    try {
        std::ifstream input(std::filesystem::u8path(path),std::ios::binary|std::ios::ate);
        if (!input) return {};
        const auto length=input.tellg();
        if (length<12 || length>256ll*1024ll*1024ll) return {};
        std::vector<uint8_t> bytes(static_cast<size_t>(length));
        input.seekg(0,std::ios::beg);
        if (!input.read(reinterpret_cast<char *>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) return {};
        return bytes;
    } catch (...) { return {}; }
}
class EngineUpdateScope {
    DoomEngine* engine_;
    bool entered_;
public:
    explicit EngineUpdateScope(DoomEngine* engine)
        : engine_(engine), entered_(doom_engine_begin_update(engine) != 0) {}
    ~EngineUpdateScope() { if (entered_) doom_engine_end_update(engine_); }
    explicit operator bool() const { return entered_; }
    EngineUpdateScope(const EngineUpdateScope&) = delete;
    EngineUpdateScope& operator=(const EngineUpdateScope&) = delete;
};
struct Assets {
    WadPtr wad;
    WadPtr otherMusicWad;
    HudPtr hud;
    std::vector<DoomMusicChoice> musicChoices;
    bool freedoom = false;
};
void collectMusicChoices(Assets& assets) {
    DoomWad* wads[] = {assets.wad.get(), assets.otherMusicWad.get()};
    for (DoomWad* wad : wads) {
        DoomLump bank{};
        if (!wad || !doom_wad_lump(wad, "GENMIDI", &bank) ||
            !bank.size || bank.size > 65536u) continue;
        const bool doom2 = doom_wad_is_doom2(wad) != 0;
        for (unsigned code = 1; code <= 45; ++code) {
            char name[9]{};
            DoomLump score{};
            if (doom_music_lump_name(doom2, code, name) &&
                doom_wad_lump(wad, name, &score) && score.size &&
                score.size <= 4u * 1024u * 1024u)
                assets.musicChoices.push_back({code, doom2});
        }
    }
}
struct PreparedMusic {
    std::unique_ptr<int16_t, PcmDeleter> pcm;
    size_t frames = 0;
    unsigned track = 0;
    bool doom2 = false;
    uint64_t epoch = 0;
    bool soundfontFallback = false;
    bool soundfontUsed = false;
};
struct PreparedEffect {
    std::unique_ptr<int16_t, PcmDeleter> pcm;
    uint32_t rate = 0, count = 0;
    std::string path;
};
struct Settings {
    std::string game = "auto", doom, doom2, music = "doom", musicSynth = "soundfont", soundfont;
    std::string controllerFire = "right", controllerCycle = "normal";
    std::string secretSound;
    bool classic = false, cheats = true, hudFit = true, bilinear = false, dynamicLights = true;
    bool randomMusic = false, freedoomFallback = true;
    int track = 1, hud = 100, weaponScale = 100, incoming = 100, outgoing = 100;
    int musicVolume = 70, effectsVolume = 100;
};

struct Runtime {
    const DlumaApiV1* api = nullptr;
    DlumaCoreV1 core{};
    DlumaEventsV1 events{};
    DlumaConfigurationV1 config{};
    DlumaAudioV1 media{};
    DlumaPlayerV1 playerApi{};
    DoomMovement movement{};
    Settings settings, mapAssetSettings;
    DoomPlayer player{};
    DoomEngine* engine = nullptr;
    DoomAudio* audio = nullptr;
    DoomGamepad* gamepad = nullptr;
    Assets assets;
    std::future<Assets> loading;
    std::future<PreparedMusic> rendering;
    std::future<PreparedEffect> secretLoading;
    PreparedEffect secretEffect;
    std::string secretRequested, bundledSecret, bundleDirectory, bundledSoundFont;
    PreparedMusic pendingMusic;
    std::shared_ptr<std::atomic<bool>> cancelRender;
    std::array<uint8_t, 320 * 200 * 4> weaponPixels{};
    std::array<uint8_t, 320 * 32 * 4> statusPixels{};
    std::array<uint8_t, 320 * 64 * 4> messagePixels{};
    std::array<uint8_t, 320 * 16 * 4> secretPixels{};
    DoomPickupFeedback pickupFeedback{};
    DoomMessages secretMessages{};
    DoomVehicleFeedback vehicleFeedback{};
    bool pickupSoundQueued = false, pickupPowerSoundQueued = false;
    bool combatSuspended = false, handBusy = false, weaponReturning = false, carryFireLatch = false;
    bool deathSoundPlayed = false;
    std::deque<std::string> nativePickupMessages;
    uint64_t nextNativePickupMessage = 0;
    uint64_t flyHudTick = 0;
    uint64_t sawStartupUntil = 0;
    uint64_t updateToken = 0, configToken = 0, musicToken = 0;
    uint64_t lastTick = 0, retryMusicAt = 0;
    uint64_t musicEpoch = 0;
    uint64_t motionSampleAt = 0;
    uint64_t fallSampleAt = 0;
    uint64_t fallActor = 0;
    unsigned fallingMs = 0;
    float fallSampleY = 0.0f;
    unsigned lastDamageSequence = 0;
    float motionX = 0.0f, motionZ = 0.0f, lastMotionSpeed = 0.0f;
    DoomNoclipDiagnostic lastNoclipDiagnostic{};
    unsigned noclipDiagnosticLogs = 0;
    const char *lastInvulnStage = nullptr;
    uint64_t nextInvulnDiagnostic = 0;
    unsigned invulnDiagnosticLogs = 0;
#if defined(DOOM_MODE_RUNTIME)
    uint64_t nextPositionLog = 0;
    std::array<unsigned, DOOM_WEAPON_COUNT> attackLogCount{};
    std::array<unsigned, DOOM_WEAPON_COUNT> damageLogCount{};
    std::array<unsigned, DOOM_WEAPON_COUNT> noiseLogCount{};
    unsigned inputLogCount = 0;
    int previousFire = -1, previousPaused = -1;
    DoomWorldRuntime world{};
    DoomWorldDisplay *worldDisplay = nullptr;
    unsigned worldWarnings = 0, hostWarnings = 0;
    bool worldDisplayWarned = false;
    bool traceWorld = false;
    unsigned worldTraceCount = 0;
    uint32_t lastSplashSequence = 0;
    unsigned splashLogCount = 0;
    uint64_t lastTraversal = UINT64_MAX;
    unsigned traversalLogCount = 0;
    unsigned damageFeedbackLogs = 0;
    int lastBodySuppression = -2;
    unsigned bodyFeedbackLogs = 0;
    uint64_t nextMovementSample = 0;
    unsigned movementSampleCount = 0;
    int lastMovementError = 0;
    std::array<unsigned, DOOM_WORLD_MAX_PROJECTILES> tracedAge{};
    std::array<int, DOOM_WORLD_MAX_PROJECTILES> tracedState{};
#endif
    uint32_t generation = 0;
    unsigned requestedTrack = 0, playingTrack = 0, resumeTrack = 0;
    bool requestedDoom2 = false, playingDoom2 = false, resumeDoom2 = false;
    bool randomMusicForMap = false, mapMusicSelected = false;
    DoomMusicChoice mapMusic{}, previousRandomMusic{};
    std::mt19937 musicRandom{static_cast<std::mt19937::result_type>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count())};
    bool scope = false, active = false, started = false, haveGeneration = false;
    bool resetCheatInput = false;
    bool reloadAssets = false, musicPlaying = false, musicOverride = false, requestOverride = false;
    bool bindingWarning = false;
    bool assetsReadyForActivation = false;
    uint64_t retryActivationAt = 0;

    template<class T> bool service(const char* id, T& output) {
        output = {};
        return api && api->get_service &&
            api->get_service(api->context, view(id), 1, &output, sizeof(output)) == DLUMA_STATUS_OK &&
            output.struct_size >= sizeof(output) && output.version == 1;
    }
    void log(const char* level, const char* message) const {
        if (!core.log) return;
        auto request = record<DlumaLogRequest>();
        auto result = resultRecord<DlumaCallResult>();
        request.level = view(level); request.message = view(message);
        core.log(core.context, &request, &result);
    }
    uint64_t now() const {
        auto request = record<DlumaNoRequest>();
        auto result = resultRecord<DlumaU64Result>();
        return core.monotonic_ms && core.monotonic_ms(core.context, &request, &result) == DLUMA_STATUS_OK
            ? result.value : 0;
    }
    std::string text(const char* key, const char* fallback) const {
        auto request = record<DlumaConfigGetRequest>();
        auto result = resultRecord<DlumaScalarResult>();
        request.key = view(key);
        if (config.get && config.get(config.context, &request, &result) == DLUMA_STATUS_OK &&
            result.value.kind == DLUMA_SCALAR_STRING && result.value.string_value.data &&
            result.value.string_value.size <= 4096) {
            return std::string(result.value.string_value.data,
                static_cast<size_t>(result.value.string_value.size));
        }
        return fallback;
    }
    int integer(const char* key, int fallback, int minimum, int maximum) const {
        auto request = record<DlumaConfigGetRequest>();
        auto result = resultRecord<DlumaScalarResult>();
        request.key = view(key);
        if (config.get && config.get(config.context, &request, &result) == DLUMA_STATUS_OK &&
            (result.value.kind == DLUMA_SCALAR_INTEGER || result.value.kind == DLUMA_SCALAR_BOOLEAN) &&
            result.value.integer_value >= minimum && result.value.integer_value <= maximum)
            return static_cast<int>(result.value.integer_value);
        return fallback;
    }
    void configuration() {
        const std::string oldMusic = settings.music;
        const std::string oldSecret = settings.secretSound;
        const std::string oldSynth = settings.musicSynth, oldFont = settings.soundfont;
        const std::string oldControllerFire=settings.controllerFire;
        const bool oldCheats = settings.cheats;
        settings.game = text("game", "auto");
        settings.doom = text("doom_wad", ""); settings.doom2 = text("doom2_wad", "");
        settings.freedoomFallback = integer("freedoom_fallback", 1, 0, 1) != 0;
        settings.music = text("music", "doom");
        settings.musicSynth = text("music_synth", "soundfont");
        settings.soundfont = text("soundfont", "");
        settings.secretSound = text("secret_sound_file", "");
        if (oldSecret!=settings.secretSound) secretEffect={};
        settings.controllerFire = text("controller_fire", "right");
        settings.controllerCycle = text("controller_cycle", "normal");
        if(oldControllerFire!=settings.controllerFire) doom_gamepad_reset(gamepad);
        settings.classic = false;
        settings.cheats = integer("cheats", 1, 0, 1) != 0;
        if (oldCheats!=settings.cheats) resetCheatInput=true;
        settings.track = integer("default_track", 1, 1, 45);
        settings.randomMusic = integer("random_map_music", 0, 0, 1) != 0;
        settings.hud = integer("hud_scale", 100, 50, 200);
        settings.hudFit = integer("hud_fit_screen", 1, 0, 1) != 0;
        settings.weaponScale = integer("weapon_scale", 100, 50, 200);
        settings.bilinear = integer("bilinear_filtering", 0, 0, 1) != 0;
        settings.dynamicLights = integer("dynamic_lights", 1, 0, 1) != 0;
        settings.incoming = integer("incoming_damage", 100, 10, 500);
        settings.outgoing = integer("outgoing_damage", 100, 10, 500);
        settings.musicVolume = integer("music_volume", 70, 0, 100);
        settings.effectsVolume = integer("effects_volume", 100, 0, 100);
        doom_engine_configure(engine, settings.classic, settings.incoming / 100.f,
            settings.outgoing / 100.f);
        doom_engine_set_texture_filtering(engine, settings.bilinear);
        doom_engine_set_dynamic_lights(engine,settings.dynamicLights);
        doom_engine_set_fire_key(engine, integer("fire_key", 162, 1, 254));
        doom_engine_set_cheats_enabled(engine,settings.cheats);
        doom_audio_music_volume(audio, settings.musicVolume / 100.f);
        (void)doom_audio_weapon_volume(audio, settings.effectsVolume / 100.f);
        if (oldMusic != settings.music) {
            musicOverride = false;
            stopMusic();
            if (active && settings.music == "doom") requestMapMusic();
        } else if ((oldSynth!=settings.musicSynth || oldFont!=settings.soundfont) && active &&
                   (settings.music=="doom" || musicOverride || requestOverride)) {
            ++musicEpoch;
            if (cancelRender) cancelRender->store(true);
            pendingMusic={};
            if (requestedTrack)
                requestMusic(requestedTrack, musicOverride || requestOverride, requestedDoom2);
            else
                requestMapMusic();
        }
        prepareSecretSound();
    }
    unsigned defaultTrack() const {
        return doom_music_setting_code(player.doom2, static_cast<unsigned>(settings.track));
    }
    DoomWad* musicWad(bool doom2) const {
        if (assets.wad && (doom_wad_is_doom2(assets.wad.get()) != 0) == doom2)
            return assets.wad.get();
        if (assets.otherMusicWad &&
            (doom_wad_is_doom2(assets.otherMusicWad.get()) != 0) == doom2)
            return assets.otherMusicWad.get();
        return nullptr;
    }
    void chooseMapMusic() {
        if (mapMusicSelected) return;
        mapMusic = {defaultTrack(), player.doom2 != 0};
        if (randomMusicForMap) {
            const auto& choices = assets.musicChoices;
            const size_t count = doom_music_eligible_count(
                choices.data(), choices.size(), previousRandomMusic);
            if (count) {
                std::uniform_int_distribution<size_t> distribution(0, count - 1);
                mapMusic = doom_music_choose(choices.data(), choices.size(),
                    previousRandomMusic, distribution(musicRandom));
            }
        }
        mapMusicSelected = true;
    }
    void requestMapMusic() {
        if (!mapMusicSelected) chooseMapMusic();
        requestMusic(mapMusic.track, false, mapMusic.doom2);
    }
    void cancel(uint64_t& token) {
        if (token && core.token_cancel) {
            auto request = record<DlumaTokenRequest>();
            auto result = resultRecord<DlumaBoolResult>();
            request.token = token;
            core.token_cancel(core.context, &request, &result);
        }
        token = 0;
    }
    bool releaseMusicLease() {
        if (!musicToken) return true;
        auto request = record<DlumaAudioMusicReleaseRequest>();
        auto result = resultRecord<DlumaBoolResult>();
        request.token = musicToken;
        const DlumaStatus status = media.music_release
            ? media.music_release(media.context, &request, &result) : DLUMA_STATUS_UNAVAILABLE;
        if (status == DLUMA_STATUS_OK || status == DLUMA_STATUS_STALE_HANDLE) {
            musicToken = 0;
            return true;
        }
        return false;
    }
    void stopMusic() {
        ++musicEpoch;
        if (cancelRender) cancelRender->store(true);
        pendingMusic = {};
        requestOverride = false;
        resumeTrack = 0;
        playingTrack = 0;
        doom_audio_music_stop(audio);
        musicPlaying = false;
        (void)releaseMusicLease();
    }
    bool acquireMusic() {
        if (musicToken) {
            auto request = record<DlumaTokenRequest>();
            auto result = resultRecord<DlumaAudioMusicStateResult>();
            request.token = musicToken;
            if (media.music_state && media.music_state(media.context, &request, &result) == DLUMA_STATUS_OK && result.phase == 2)
                return true;
            doom_audio_music_stop(audio);
            const bool wasPlaying = musicPlaying;
            if (wasPlaying) { resumeTrack = playingTrack; resumeDoom2 = playingDoom2; }
            musicPlaying = false;
            (void)releaseMusicLease();
            if (musicToken) return false;
            if (wasPlaying && !pendingMusic.pcm && !rendering.valid()) requestMusic(resumeTrack, musicOverride, resumeDoom2);
        }
        if (now() < retryMusicAt) return false;
        retryMusicAt = now() + 1000;
        auto request = record<DlumaAudioMusicAcquireRequest>();
        auto result = resultRecord<DlumaTokenResult>();
        if (!media.music_acquire || media.music_acquire(media.context, &request, &result) != DLUMA_STATUS_OK)
            return false;
        musicToken = result.token;
        return musicToken != 0;
    }
    void requestMusic(unsigned track, bool overrideRequest = false, int selectedDoom2 = -1) {
        const bool doom2 = selectedDoom2 < 0 ? player.doom2 != 0 : selectedDoom2 != 0;
        DoomWad* wad = musicWad(doom2);
        if (!wad) return;
        char name[9]{};
        DoomLump music{}, bank{};
        if (!doom_music_lump_name(doom2, track, name) ||
            !doom_wad_lump(wad, name, &music) ||
            !doom_wad_lump(wad, "GENMIDI", &bank) ||
            music.size > 4u * 1024u * 1024u || bank.size > 65536u) return;
        requestedTrack = track;
        requestedDoom2 = doom2;
        requestOverride = overrideRequest;
        pendingMusic = {};
        // A pending render finishes before starting another; the latest request wins.
        if (rendering.valid()) return;
        std::vector<uint8_t> score(music.data, music.data + music.size);
        std::vector<uint8_t> instruments(bank.data, bank.data + bank.size);
        const uint64_t epoch = musicEpoch;
        cancelRender = std::make_shared<std::atomic<bool>>(false);
        const auto cancel = cancelRender;
        const bool useSoundFont=settings.musicSynth=="soundfont";
        const std::string fontPath=settings.soundfont.empty() ? bundledSoundFont : settings.soundfont;
        rendering = std::async(std::launch::async,
            [score = std::move(score), instruments = std::move(instruments), track, doom2, epoch, cancel,
             useSoundFont, fontPath]() {
                PreparedMusic output;
                int16_t* pcm = nullptr;
                auto canceled = [](void* context) -> int {
                    return static_cast<std::atomic<bool>*>(context)->load() ? 1 : 0;
                };
                if (useSoundFont && !cancel->load()) {
                    auto font=readSoundFont(fontPath);
                    if (!font.empty() && !cancel->load() &&
                        doom_synth_render_sf2_cancelable(score.data(),score.size(),font.data(),font.size(),
                            &pcm,&output.frames,44100,canceled,cancel.get()))
                        output.pcm.reset(pcm);
                    output.soundfontFallback=!output.pcm && !cancel->load();
                    output.soundfontUsed=output.pcm!=nullptr;
                }
                if (!output.pcm && !cancel->load() &&
                    doom_synth_render_cancelable(score.data(), score.size(), instruments.data(),
                        instruments.size(), &pcm, &output.frames, 44100, canceled, cancel.get()))
                    output.pcm.reset(pcm);
                output.track = track;
                output.doom2 = doom2;
                output.epoch = epoch;
                return output;
            });
    }
    void musicTick() {
        if (!active || !audio) return;
        const bool wantsDoom = musicOverride || requestOverride || settings.music == "doom";
        if (!wantsDoom && settings.music == "host") {
            if (musicPlaying || musicToken || pendingMusic.pcm ||
                (cancelRender && !cancelRender->load())) stopMusic();
            return;
        }
        if (!wantsDoom) { (void)acquireMusic(); return; }
        if (musicToken && !acquireMusic()) return;
        if (rendering.valid() && rendering.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            PreparedMusic result = rendering.get();
            if (result.track != requestedTrack || result.doom2 != requestedDoom2 || result.epoch != musicEpoch) { requestMusic(requestedTrack, requestOverride, requestedDoom2); return; }
            if (result.soundfontFallback)
                log("WARN","Dooming Light could not load the SoundFont; using Classic OPL.");
            if (!result.pcm) {
                log("WARN", "Dooming Light could not prepare the selected music track.");
                requestOverride = false;
                if (!musicPlaying && resumeTrack) {
                    const unsigned previous = resumeTrack;
                    resumeTrack = 0;
                    requestMusic(previous, musicOverride, resumeDoom2);
                } else if (!musicPlaying && settings.music != "off") {
                    (void)releaseMusicLease();
                }
                return;
            }
            pendingMusic = std::move(result);
        }
        if (pendingMusic.pcm) {
            if (!acquireMusic()) return;
            if (doom_audio_music_adopt(audio, pendingMusic.pcm.get(), pendingMusic.frames, 44100,
                    settings.musicVolume / 100.f)) {
                (void)pendingMusic.pcm.release();
                musicPlaying = true;
                char message[112]{};
                std::snprintf(message,sizeof(message),"Dooming Light music: synth=%s track=%u",
                    pendingMusic.soundfontUsed ? "SoundFont" : "Classic OPL",pendingMusic.track);
                log("INFO",message);
                playingTrack = pendingMusic.track;
                playingDoom2 = pendingMusic.doom2;
                previousRandomMusic = {playingTrack, playingDoom2};
                resumeTrack = 0;
                if (requestOverride) musicOverride = true;
                requestOverride = false;
            }
            else {
                requestOverride = false;
                if (!musicPlaying && resumeTrack) {
                    const unsigned previous = resumeTrack;
                    resumeTrack = 0;
                    requestMusic(previous, musicOverride, resumeDoom2);
                } else if (!musicPlaying && settings.music != "off") {
                    (void)releaseMusicLease();
                }
            }
            pendingMusic = {};
        }
    }
    bool sound(const char* name, float gain = 1.0f) {
        DoomSound effect{};
        if (audio && assets.wad && doom_wad_sound(assets.wad.get(), name, &effect)) {
            const bool played=doom_audio_play_pcm(audio, effect.samples, effect.count, effect.rate,
                settings.effectsVolume / 100.f * std::clamp(gain,0.0f,1.0f))!=0;
            doom_sound_free(&effect);
            return played;
        }
        return false;
    }
    void prepareSecretSound() {
        const std::string &selected=settings.secretSound.empty() ? bundledSecret : settings.secretSound;
        if (secretLoading.valid()) {
            if (secretLoading.wait_for(std::chrono::milliseconds(0))!=std::future_status::ready) return;
            try {
                PreparedEffect effect=secretLoading.get();
                if (effect.path==selected) {
                    secretEffect=std::move(effect);
                    if (!secretEffect.pcm) log("WARN","Dooming Light: secret sound file could not be loaded; using the WAD sound.");
                }
            } catch (...) {
                log("WARN","Dooming Light: secret sound file could not be loaded; using the WAD sound.");
            }
        }
        if (secretRequested==selected) return;
        secretRequested=selected;
        if (secretRequested.empty()) { secretEffect={}; return; }
        try {
            const std::string path=secretRequested;
            secretLoading=std::async(std::launch::async,[path] {
                PreparedEffect effect; effect.path=path;
                DoomSound decoded{};
                if (doom_sound_file_load(path.c_str(),&decoded)) {
                    effect.pcm.reset(decoded.samples); effect.rate=decoded.rate; effect.count=decoded.count;
                }
                return effect;
            });
        } catch (...) {
            log("WARN","Dooming Light: secret sound file could not be loaded; using the WAD sound.");
        }
    }
    void secretSound() {
        const std::string &selected=settings.secretSound.empty() ? bundledSecret : settings.secretSound;
        if (audio && secretEffect.pcm && secretEffect.path==selected &&
            doom_audio_play_pcm(audio,secretEffect.pcm.get(),secretEffect.count,secretEffect.rate,
                settings.effectsVolume/100.f)) return;
        if (!sound("DSSECRET") && !sound("DSGETPOW")) sound("DSITEMUP");
    }
    void presentSecret(bool hudReady) {
        const char *message=doom_messages_text(&secretMessages);
        const bool visible=hudReady && message &&
            doom_hud_render_secret(assets.hud.get(),secretPixels.data(),secretPixels.size(),message);
        (void)doom_engine_present_secret(engine,visible ? secretPixels.data() : nullptr,visible);
    }
    void weaponSound(unsigned soundId, const char* name) {
        if (soundId == DOOM_SOUND_SAWIDL && now() < sawStartupUntil) return;
        DoomSound effect{};
        if (!audio || !assets.wad || !doom_wad_sound(assets.wad.get(), name, &effect)) return;
        if (soundId == DOOM_SOUND_SAWUP) {
            if (doom_audio_weapon_once_pcm(audio, soundId + 1, effect.samples, effect.count,
                    effect.rate, settings.effectsVolume / 100.f))
                sawStartupUntil = now() + static_cast<uint64_t>(effect.count) * 1000u / effect.rate;
        } else {
            sawStartupUntil = 0;
            (void)doom_audio_weapon_loop_pcm(audio, soundId + 1, effect.samples, effect.count,
                effect.rate, settings.effectsVolume / 100.f);
        }
        doom_sound_free(&effect);
    }
    void stopWeaponSound() {
        doom_audio_weapon_stop(audio);
        sawStartupUntil = 0;
    }
    void resetMotion() {
        motionSampleAt=0; lastMotionSpeed=0;
        fallSampleAt=0; fallingMs=0; fallActor=0;
        doom_hud_motion_reset(assets.hud.get());
    }
    bool fallingFace(uint64_t timestamp, const DoomEngineFrame &frame, bool onFoot) {
        float x=0, y=0, z=0;
        if (!onFoot || player.health<=0 || frame.doom_dead || frame.death_commit ||
            frame.paused || frame.noclip || frame.edge_active || frame.ladder_active ||
            !frame.airborne_valid || !frame.airborne || !frame.airborne_actor ||
            !doom_engine_position(engine,&x,&y,&z)) {
            fallSampleAt=0; fallingMs=0; fallActor=0; return false;
        }
        if (fallActor!=frame.airborne_actor) { fallSampleAt=0; fallingMs=0; fallActor=frame.airborne_actor; }
        if (fallSampleAt && timestamp==fallSampleAt) return fallingMs>=1000;
        if (fallSampleAt && timestamp>fallSampleAt && timestamp-fallSampleAt<=250) {
            const unsigned elapsed=static_cast<unsigned>(timestamp-fallSampleAt);
            const float velocity=(y-fallSampleY)*1000.0f/static_cast<float>(elapsed);
            if (std::isfinite(velocity) && velocity<-0.25f && velocity>-150.0f)
                fallingMs=std::min(fallingMs+elapsed,60000u);
            else fallingMs=0;
        } else fallingMs=0;
        fallSampleAt=timestamp; fallSampleY=y;
        return fallingMs>=1000;
    }
    float movementSpeed(uint64_t timestamp, bool movingAllowed) {
        float x=0, y=0, z=0, speed=0;
        if (!movingAllowed) {
            resetMotion(); return 0;
        }
        if (!doom_engine_position(engine,&x,&y,&z)) {
            motionSampleAt=0; lastMotionSpeed=0;
            doom_hud_bob_reset(assets.hud.get()); return 0;
        }
        if (motionSampleAt && timestamp==motionSampleAt) return lastMotionSpeed;
        if (motionSampleAt && timestamp>motionSampleAt && timestamp-motionSampleAt<=250) {
            const float seconds=static_cast<float>(timestamp-motionSampleAt)/1000.0f;
            const float dx=x-motionX, dz=z-motionZ;
            speed=std::sqrt(dx*dx+dz*dz)/seconds;
            if (!std::isfinite(speed) || speed>24.0f) {
                doom_hud_bob_reset(assets.hud.get()); speed=0;
            }
        }
        motionX=x; motionZ=z; motionSampleAt=timestamp; lastMotionSpeed=speed;
        return speed;
    }
#if defined(DOOM_MODE_RUNTIME)
    bool projectileBasis(DoomWorldFrame& frame) const {
        DoomVec3 eye{}, forward{};
        if (!frame.host || !frame.host->aim || !frame.host->sweep ||
            !doom_engine_shot_basis(engine, &eye, &forward)) return false;
        DoomTrace aim{}, clearance{};
        if (!frame.host->aim(frame.host->context, eye, forward, 2048.0f, &aim)) return false;
        // Keep the visible launch near the view while retaining the eye ray's aim.
        // Scale offsets with the current player height instead of a fixed world offset.
        const float forwardOffset=std::clamp(frame.player_height*0.14f,2.0f,24.0f);
        const float downOffset=std::clamp(frame.player_height*0.10f,1.0f,16.0f);
        DoomVec3 muzzle{eye.x+forward.x*forwardOffset, eye.y+forward.y*forwardOffset,
                        eye.z+forward.z*forwardOffset-downOffset};
        if (!frame.host->sweep(frame.host->context, eye, muzzle, 0.0f, &clearance)) return false;
        if (clearance.hit) {
            // Keep the spawn on the camera side of an intervening wall.
            DoomVec3 delta{clearance.point.x-eye.x, clearance.point.y-eye.y,
                           clearance.point.z-eye.z};
            const float length=std::sqrt(delta.x*delta.x+delta.y*delta.y+delta.z*delta.z);
            const float fraction=length>1.0f ? (length-1.0f)/length : 0.0f;
            muzzle={eye.x+delta.x*fraction,eye.y+delta.y*fraction,eye.z+delta.z*fraction};
        }
        DoomVec3 direction{aim.point.x-muzzle.x,aim.point.y-muzzle.y,aim.point.z-muzzle.z};
        const float length=std::sqrt(direction.x*direction.x+direction.y*direction.y+
                                     direction.z*direction.z);
        if (!std::isfinite(length) || length<0.0001f) return false;
        frame.shot_origin=muzzle;
        frame.shot_direction={direction.x/length,direction.y/length,direction.z/length};
        return true;
    }
    void logWorld() {
        if (!traceWorld || worldTraceCount>=96) return;
        for (unsigned i=0; i<DOOM_WORLD_MAX_PROJECTILES && worldTraceCount<96; ++i) {
            const DoomProjectile& p=world.world.projectiles[i];
            const int state=static_cast<int>(p.state);
            const bool changed=state!=tracedState[i];
            const unsigned age=p.state==DOOM_PROJECTILE_IMPACT ? p.impact_age : p.age;
            if ((changed || (p.state!=DOOM_PROJECTILE_FREE && age>=tracedAge[i]+4u)) &&
                (p.state!=DOOM_PROJECTILE_FREE || tracedState[i]!=DOOM_PROJECTILE_FREE)) {
                char message[288]{};
                std::snprintf(message,sizeof(message),
                    "Doom lab projectile: slot=%u weapon=%d state=%d age=%u origin=%.2f,%.2f,%.2f position=%.2f,%.2f,%.2f direction=%.4f,%.4f,%.4f errors=%u",
                    i,static_cast<int>(p.weapon),state,age,
                    static_cast<double>(p.origin.x),static_cast<double>(p.origin.y),static_cast<double>(p.origin.z),
                    static_cast<double>(p.position.x),static_cast<double>(p.position.y),static_cast<double>(p.position.z),
                    static_cast<double>(p.direction.x),static_cast<double>(p.direction.y),static_cast<double>(p.direction.z),
                    world.world.errors);
                log("INFO",message); ++worldTraceCount; tracedAge[i]=age;
            }
            if (changed) tracedAge[i]=age;
            tracedState[i]=state;
        }
    }
#if defined(DOOM_MODE_LAB)
    void logSplash() {
        const uint32_t latest=world.world.splash_sequence;
        if (latest<=lastSplashSequence) return;
        const uint32_t first=latest-lastSplashSequence>DOOM_SPLASH_HISTORY
            ? latest-DOOM_SPLASH_HISTORY+1u : lastSplashSequence+1u;
        for (uint32_t sequence=first; sequence<=latest && splashLogCount<16; ++sequence) {
            const DoomSplashTelemetry& t=world.world.splash_history[(sequence-1u)%DOOM_SPLASH_HISTORY];
            if (t.sequence!=sequence) continue;
            char message[360]{};
            std::snprintf(message,sizeof(message),
                "Doom lab blast: seq=%u direct=%llu direct_damage=%d direct_result=%d query=%u candidates=%u capacity=%u outside=%u blocked=%u visible=%u damaged=%u failed=%u query_failed=%u impact=%.2f,%.2f,%.2f",
                sequence,static_cast<unsigned long long>(t.direct_target),t.direct_damage,t.direct_result,
                t.query_ok,t.candidates,t.at_capacity,t.outside,t.blocked,t.visible,t.damaged,t.damage_failed,t.query_failed,
                static_cast<double>(t.impact.x/32.0f),static_cast<double>(t.impact.y/32.0f),static_cast<double>(t.impact.z/32.0f));
            log("INFO",message); ++splashLogCount;
            for (unsigned i=0; i<t.sample_count && i<DOOM_SPLASH_SAMPLE_LIMIT; ++i) {
                const DoomSplashSample& s=t.samples[i];
                std::snprintf(message,sizeof(message),
                    "Doom lab blast target: seq=%u target=%llu center_m=%.3f edge_m=%.3f radius_m=%.3f damage=%d state=%d",
                    sequence,static_cast<unsigned long long>(s.target),
                    static_cast<double>(s.center_distance/32.0f),static_cast<double>(s.edge_distance/32.0f),
                    static_cast<double>(s.radius/32.0f),s.damage,static_cast<int>(s.state));
                log("INFO",message);
            }
        }
        lastSplashSequence=latest;
    }
#endif
    void logDamage(const DoomDamageObservation& observation) const {
        char before[32] = "unavailable", after[32] = "unavailable", message[288]{};
        if (observation.before_valid)
            std::snprintf(before, sizeof(before), "%.3f", static_cast<double>(observation.before));
        if (observation.after_valid)
            std::snprintf(after, sizeof(after), "%.3f", static_cast<double>(observation.after));
        std::snprintf(message, sizeof(message),
            "Doom lab damage: weapon=%d target=%d native=%.3f physics=%.3f before=%s after=%s removed=%d mesh=%d bone=%d model=%d reason=%d",
            observation.weapon, observation.target_id, static_cast<double>(observation.native_damage),
            static_cast<double>(observation.physics_damage),
            before, after, observation.removed,observation.mesh_element,observation.hit_bone,observation.model_resolution,observation.model_reason);
        log("INFO", message);
    }
#endif
    static void onDoomEvent(void* context, const DoomEvent* event) {
        Runtime& self = *static_cast<Runtime*>(context);
        if (!event) return;
        if (self.combatSuspended && (event->type==DOOM_EVENT_ATTACK ||
                event->type==DOOM_EVENT_WEAPON_FIRED || event->type==DOOM_EVENT_SOUND)) return;
        if (event->type == DOOM_EVENT_ATTACK) {
            const int result = doom_engine_attack(self.engine, event);
            if (event->weapon==DOOM_CHAINGUN)
                (void)doom_engine_muzzle_flash(self.engine,DOOM_CHAINGUN);
#if defined(DOOM_MODE_LAB)
            const bool dispatched=std::strcmp(doom_engine_attack_reason(self.engine), "damage_dispatched")==0;
            if (event->weapon >= 0 && event->weapon < DOOM_WEAPON_COUNT &&
                (dispatched ? self.damageLogCount : self.attackLogCount)[static_cast<size_t>(event->weapon)]++ < 16) {
                unsigned actors=0, tested=0, hit=0;
                doom_engine_attack_counts(self.engine, &actors, &tested, &hit);
                char message[256]{};
                std::snprintf(message, sizeof(message),
                    "Doom lab attack: weapon=%d damage=%d result=%d reason=%s actors=%u tested=%u boxes_hit=%u",
                    event->weapon, event->damage, result, doom_engine_attack_reason(self.engine),
                    actors, tested, hit);
                self.log("INFO", message);
                DoomDamageObservation observation{};
                if (dispatched && doom_engine_damage_observation(self.engine, &observation))
                    self.logDamage(observation);
            }
#else
            (void)result;
#endif
        }
        else if (event->type == DOOM_EVENT_WEAPON_FIRED) {
            const int result=doom_engine_report_shot(self.engine,static_cast<DoomWeapon>(event->weapon));
            if (event->weapon!=DOOM_CHAINGUN)
                (void)doom_engine_muzzle_flash(self.engine,static_cast<DoomWeapon>(event->weapon));
#if defined(DOOM_MODE_LAB)
            if (event->weapon>=0 && event->weapon<DOOM_WEAPON_COUNT &&
                self.noiseLogCount[static_cast<size_t>(event->weapon)]++<16) {
                char message[112]{};
                std::snprintf(message,sizeof(message),"Doom lab weapon noise: weapon=%d result=%d",event->weapon,result);
                self.log("INFO",message);
            }
#else
            (void)result;
#endif
        }
        else if (event->type == DOOM_EVENT_CHEAT) {
            doom_engine_reset_damage(self.engine);
            doom_engine_sync_powerups(self.engine);
            const char *message=nullptr;
            switch (event->value) {
            case DOOM_CHEAT_GOD:
                message=self.player.god ? "Degreelessness Mode On" : "Degreelessness Mode Off";
                break;
            case DOOM_CHEAT_AMMO: message="Ammo (no keys) Added"; break;
            case DOOM_CHEAT_AMMO_KEYS: message="Very Happy Ammo Added"; break;
            case DOOM_CHEAT_CHOPPERS: message="... doesn't suck - GM"; break;
            case DOOM_CHEAT_BEHOLD_MENU: message="inVuln, Str, Inviso, Rad, Allmap, or Lite-amp"; break;
            case DOOM_CHEAT_BEHOLD_POWER: message="Power-up Toggled"; break;
            case DOOM_CHEAT_NOCLIP:
                if (!doom_engine_request_noclip(self.engine)) message="Noclip Unavailable";
                break;
            default: break;
            }
            if (message) {
                doom_engine_allow_native_pickups(self.engine,0);
                (void)doom_messages_post(&self.pickupFeedback.messages,message,DOOM_MESSAGE_IMPORTANT,4000);
            }
        }
        else if (event->type == DOOM_EVENT_SOUND) {
            static const char* const names[] = {"DSPISTOL", "DSSHOTGN", "DSSGCOCK", "DSDSHTGN",
                "DSDBOPN", "DSDBLOAD", "DSDBCLS", "DSSAWUP", "DSSAWIDL", "DSSAWFUL",
                "DSRLAUNC", "DSPLASMA", "DSBFG"};
            if (event->value >= 0 && static_cast<size_t>(event->value) < sizeof(names) / sizeof(names[0])) {
                if (event->value == DOOM_SOUND_SAWUP || event->value == DOOM_SOUND_SAWIDL ||
                    event->value == DOOM_SOUND_SAWFUL)
                    self.weaponSound(static_cast<unsigned>(event->value), names[event->value]);
                else self.sound(names[event->value]);
            }
        }
        else if (event->type == DOOM_EVENT_MUSIC) {
            char name[9]{}; DoomLump lump{};
            if (doom_music_lump_name(self.player.doom2, static_cast<unsigned>(event->value), name) &&
                doom_wad_lump(self.assets.wad.get(), name, &lump)) {
                (void)doom_messages_post(&self.pickupFeedback.messages,"Music Change",DOOM_MESSAGE_IMPORTANT,4000);
                self.requestMusic(static_cast<unsigned>(event->value), true);
            } else (void)doom_messages_post(&self.pickupFeedback.messages,"IMPOSSIBLE SELECTION",DOOM_MESSAGE_IMPORTANT,4000);
        }
    }
#if defined(DOOM_MODE_RUNTIME)
    static void onPickup(void* context, DoomDrop kind, int amount) {
        Runtime& self = *static_cast<Runtime*>(context);
        doom_pickup_feedback_record(&self.pickupFeedback, kind, amount);
        doom_engine_sync_powerups(self.engine);
        if (amount > 0) {
            self.pickupSoundQueued = true;
            if (kind >= DOOM_DROP_INVULNERABILITY && kind < DOOM_DROP_COUNT)
                self.pickupPowerSoundQueued = true;
        }
    }
#endif
    void beginAssets() {
        assetsReadyForActivation=false;
        if (loading.valid()) { reloadAssets = true; return; }
        Settings selected = mapAssetSettings;
        selected.randomMusic = randomMusicForMap;
        const std::string directory = bundleDirectory;
        loading = std::async(std::launch::async, [selected, directory]() {
            Assets result;
            char path[4097]{};
            int fallback = 0;
            const int game = selected.game=="doom" ? 1 : selected.game=="doom2" ? 2 :
                selected.game=="freedoom1" ? 3 : selected.game=="freedoom2" ? 4 : 0;
            const bool found = doom_select_iwad(game, selected.doom.c_str(), selected.doom2.c_str(),
                selected.freedoomFallback, directory.c_str(), path, sizeof(path), &fallback) != 0;
            result.freedoom = fallback != 0 && game<3;
            if (found) {
                result.wad.reset(doom_wad_open(path));
                if (result.wad) result.hud.reset(doom_hud_create(result.wad.get()));
                if (result.wad && selected.randomMusic) {
                    const int otherGame = doom_wad_is_doom2(result.wad.get()) ? 1 : 2;
                    const int otherChoice = game>=3 ? otherGame+2 : otherGame;
                    if (doom_select_iwad(otherChoice, selected.doom.c_str(), selected.doom2.c_str(),
                            selected.freedoomFallback, directory.c_str(), path, sizeof(path), nullptr))
                        result.otherMusicWad.reset(doom_wad_open(path));
                    collectMusicChoices(result);
                }
            }
            return result;
        });
    }
    void deactivate() {
        active = false;
        doom_gamepad_reset(gamepad);
        retryActivationAt=0;
        combatSuspended=false; handBusy=false; weaponReturning=false; carryFireLatch=false;
        deathSoundPlayed=false;
        nativePickupMessages.clear(); nextNativePickupMessage=0;
        flyHudTick=0;
        lastNoclipDiagnostic={}; noclipDiagnosticLogs=0;
        lastInvulnStage=nullptr; nextInvulnDiagnostic=0; invulnDiagnosticLogs=0;
        lastDamageSequence=0;
        resetMotion();
        doom_pickup_feedback_reset(&pickupFeedback);
        doom_messages_reset(&secretMessages);
        doom_vehicle_feedback_reset(&vehicleFeedback);
        pickupSoundQueued = false;
        pickupPowerSoundQueued = false;
        sawStartupUntil = 0;
#if defined(DOOM_MODE_RUNTIME)
        if (worldDisplay && engine) (void)doom_world_display_close(worldDisplay, engine);
        doom_world_runtime_reset(&world, 0);
        worldWarnings = hostWarnings = 0;
#endif
        if (movement.player.variable_reset) (void)doom_movement_restore(&movement);
        doom_engine_deactivate(engine);
        stopMusic();
        doom_audio_stop_effects(audio);
        lastTick = 0;
    }
    void tick(uint32_t mapGeneration) {
        prepareSecretSound();
        if (!scope) return;
        if (!haveGeneration || generation != mapGeneration) {
            deactivate(); generation = mapGeneration; haveGeneration = true;
#if defined(DOOM_MODE_LAB)
            attackLogCount.fill(0); damageLogCount.fill(0); inputLogCount = 0; previousFire = previousPaused = -1;
            noiseLogCount.fill(0); nextMovementSample=0; movementSampleCount=0; lastMovementError=0;
            worldTraceCount=0; tracedAge.fill(0); tracedState.fill(DOOM_PROJECTILE_FREE);
            lastSplashSequence=0; splashLogCount=0;
            lastTraversal=UINT64_MAX; traversalLogCount=0;
            lastDamageSequence=0; damageFeedbackLogs=0; lastBodySuppression=-2; bodyFeedbackLogs=0;
#endif
            musicOverride = false;
            mapAssetSettings = settings;
            randomMusicForMap = mapAssetSettings.randomMusic;
            mapMusicSelected = false;
            mapMusic = {};
            beginAssets();
        }
        if (loading.valid() && loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            Assets loaded = loading.get();
            if (reloadAssets) { reloadAssets = false; beginAssets(); return; }
            assets = std::move(loaded);
            if (!assets.wad || !assets.hud) { log("ERROR", "Dooming Light could not load a WAD. Set a valid Doom WAD path or enable Use Freedoom fallback in Mods."); return; }
            if (assets.freedoom)
                log("ERROR", "Dooming Light could not find your selected Doom WADs; using Freedoom as backup.");
            doom_reset(&player, doom_wad_is_doom2(assets.wad.get()));
#if defined(DOOM_MODE_RUNTIME)
            doom_world_runtime_reset(&world, generation);
#endif
            doom_engine_reset_damage(engine);
            DoomSound pain{},jump{},death{};
            const bool painReady=audio && doom_wad_sound(assets.wad.get(),"DSPLPAIN",&pain) &&
                doom_wad_sound(assets.wad.get(),"DSOOF",&jump) &&
                doom_wad_sound(assets.wad.get(),"DSPLDETH",&death);
            doom_sound_free(&pain); doom_sound_free(&jump); doom_sound_free(&death);
            doom_engine_set_voice_replacement(engine,painReady);
            assetsReadyForActivation=true;
        }
        if (!active && assetsReadyForActivation) {
            if (now()<retryActivationAt) return;
            retryActivationAt=now()+250;
            if (!doom_engine_activate(engine, generation)) {
                if (!bindingWarning) {
                    log("INFO", "Dooming Light is waiting for the player and game services; activation will be retried.");
                    bindingWarning = true;
                }
                return;
            }
            active = true;
            retryActivationAt=0;
            if (settings.music == "doom") requestMapMusic();
        }
        if (!active) { (void)releaseMusicLease(); return; }
        DoomEngineFrame frame{};
        if (!doom_engine_poll(engine, &frame) || !frame.ready) {
            deactivate();
            beginAssets();
            return;
        }
        doom_engine_allow_native_pickups(engine,0);
        doom_audio_poll(audio);
        doom_audio_pause(audio, frame.paused && !frame.noclip_input && !frame.chat_view);
        if (!frame.paused || frame.noclip_input || frame.chat_view)
            for (unsigned i=0;i<std::min(frame.chat.notifications,16u);++i)
                (void)sound(player.doom2 ? "DSRADIO" : "DSTINK");
        if (frame.invuln_requested && invulnDiagnosticLogs<64 &&
            (frame.invuln_stage!=lastInvulnStage || now()>=nextInvulnDiagnostic)) {
            char message[224]{};
            std::snprintf(message,sizeof(message),
                "Dooming Light inverse palette: stage=%s calls=%u stock=%u substitutions=%u",
                frame.invuln_stage ? frame.invuln_stage : "unknown",frame.invuln_shader_calls,
                frame.invuln_stock_binds,frame.invuln_substitutions);
            log("INFO",message);lastInvulnStage=frame.invuln_stage;
            nextInvulnDiagnostic=now()+1000;++invulnDiagnosticLogs;
        }
        DoomNoclipDiagnostic noclipState=frame.noclip_diagnostic;
        noclipState.raw_keys=noclipState.owned_keys=0;
        if (noclipDiagnosticLogs<128 && (!noclipDiagnosticLogs ||
                std::memcmp(&lastNoclipDiagnostic,&noclipState,sizeof(noclipState)))) {
            const auto &d=frame.noclip_diagnostic;
            char message[384]{};
            std::snprintf(message,sizeof(message),
                "Dooming Light noclip: enabled=%d tracked=%d validation=%d camera=%d saved=%d owner=%d actor=%d input=%d transition=%d ready=%d free=%d captured=%d raw=%u owned=%u",
                d.enabled,d.tracked,d.validation,d.camera_mode,d.saved,d.owner,d.actor,d.input_mode,d.last_transition,
                d.input_ready,d.free_camera,d.captured,d.raw_keys,d.owned_keys);
            log("INFO",message); lastNoclipDiagnostic=noclipState; ++noclipDiagnosticLogs;
        }
#if defined(DOOM_MODE_LAB)
        for (unsigned i=0; i<frame.voice_observation_count && i<64; ++i) {
            const auto &v=frame.voice_observations[i];
            char message[224]{};
            std::snprintf(message,sizeof(message),"Doom lab voice: kind=%u category=%u caller=%08X cue=%s",v.kind,v.category,v.caller_rva,v.cue);
            log("INFO",message);
        }
#endif
        if (frame.noclip_notice) {
            const char *message=frame.noclip_notice==1 ? "No Clipping Mode ON" :
                frame.noclip_notice==2 ? "No Clipping Mode OFF" : "Noclip Unavailable";
            (void)doom_messages_post(&pickupFeedback.messages,message,DOOM_MESSAGE_IMPORTANT,4000);
        }
        if (frame.noclip) {
            (void)doom_gamepad_sample(gamepad,0,settings.controllerFire=="left",0);
            combatSuspended=true;
            doom_cancel_attack(&player); stopWeaponSound(); resetMotion();
            handBusy=false; weaponReturning=false;
            if (resetCheatInput) { player.cheat_len=0; player.cheat[0]=0; resetCheatInput=false; }
            if (frame.noclip_input) {
                const uint64_t stamp=now();
                const unsigned elapsed=flyHudTick && stamp>=flyHudTick
                    ? static_cast<unsigned>(std::min<uint64_t>(stamp-flyHudTick,250)) : 0;
                flyHudTick=stamp;
                doom_pickup_feedback_begin_frame(&pickupFeedback,elapsed);
                doom_messages_tick(&secretMessages,elapsed);
                if (settings.cheats)
                    for (unsigned i=0; i<std::min(frame.cheat_count,16u); ++i)
                        doom_cheat_char(&player,frame.cheat_chars[i],onDoomEvent,this);
                if (doom_hud_render_layers(assets.hud.get(),&player,
                        weaponPixels.data(),weaponPixels.size(),statusPixels.data(),statusPixels.size(),
                        messagePixels.data(),messagePixels.size(),doom_pickup_feedback_text(&pickupFeedback))) {
                    weaponPixels.fill(0);
                    const bool chatReady=doom_hud_render_chat(assets.hud.get(),messagePixels.data(),messagePixels.size(),&frame.chat,
                        pickupFeedback.messages.remaining_ms!=0)!=0;
                    const bool presented=doom_engine_present_layers(engine,weaponPixels.data(),statusPixels.data(),messagePixels.data(),
                        settings.hudFit ? 0.f : settings.hud/100.f,settings.weaponScale/100.f,0,0)!=0;
                    doom_chat_presented(chatReady && presented);
                    presentSecret(presented);
                }
            } else flyHudTick=0;
            lastTick=0;
            musicTick();
            return;
        }
        flyHudTick=0;
#if defined(DOOM_MODE_LAB)
        if ((frame.fire != previousFire || frame.paused != previousPaused) && inputLogCount++ < 32) {
            char message[192]{};
            std::snprintf(message, sizeof(message),
                "Doom lab input: fire=%d paused=%d alive=%d weapon=%d health=%d cooldown=%u delay=%u",
                frame.fire, frame.paused, frame.alive, player.weapon, player.health,
                player.cooldown, player.fire_delay);
            log("INFO", message);
        }
        previousFire = frame.fire; previousPaused = frame.paused;
        if (frame.damage_sequence && frame.damage_sequence!=lastDamageSequence && damageFeedbackLogs++<64) {
            char message[320]{};
            std::snprintf(message,sizeof(message),
                "Doom lab incoming: seq=%u damage=%d side=%d face=%d health=%d armor=%d hooks=%u local=%u reads=%u matched=%u reason=%d hit_thread=%u update_thread=%u",
                frame.damage_sequence,frame.damage_amount,frame.damage_side,player.face_index,player.health,player.armor,
                frame.direction.hook_calls,frame.direction.local_calls,frame.direction.side_reads,frame.direction.matched_reads,
                frame.direction.last_reason,frame.damage_thread,frame.update_thread);
            log("INFO",message);
        }
        if (frame.body_suppression!=lastBodySuppression && bodyFeedbackLogs++<32) {
            char message[96]{};
            std::snprintf(message,sizeof(message),"Doom lab body suppression: %d",frame.body_suppression);
            log("INFO",message);
        }
        lastBodySuppression=frame.body_suppression;
        const uint64_t traversal=static_cast<uint64_t>(frame.move_state) |
            (static_cast<uint64_t>(frame.edge_state)<<16) |
            (static_cast<uint64_t>(frame.edge_active!=0)<<48) |
            (static_cast<uint64_t>(frame.ladder_active!=0)<<49) |
            (static_cast<uint64_t>(frame.parkour)<<50);
        if (traversal!=lastTraversal && traversalLogCount++<64) {
            char message[192]{};
            std::snprintf(message,sizeof(message),
                "Doom lab traversal: kind=%d move=%u edge=%u edge_active=%d ladder=%d",
                frame.parkour,frame.move_state,frame.edge_state,frame.edge_active,frame.ladder_active);
            log("INFO",message);
        }
        lastTraversal=traversal;
        if (now() >= nextPositionLog) {
            float x = 0, y = 0, z = 0;
            if (doom_engine_position(engine, &x, &y, &z)) {
                char message[160]{};
                std::snprintf(message, sizeof(message), "Dooming Light lab position: %.6g %.6g %.6g", x, y, z);
                log("INFO", message);
            }
            nextPositionLog = now() + 500;
        }
#endif
        if (!frame.paused && frame.damage_sequence && frame.damage_sequence!=lastDamageSequence && player.health>0) {
            if (!sound("DSPLPAIN")) doom_engine_set_voice_replacement(engine,0);
        }
        lastDamageSequence=frame.damage_sequence;
        if (!frame.paused && frame.jump_sounds && player.health>0 && !sound("DSOOF"))
            doom_engine_set_voice_replacement(engine,0);
        if (frame.paused) {
            (void)doom_gamepad_sample(gamepad,0,settings.controllerFire=="left",0);
            stopWeaponSound(); resetMotion(); lastTick = 0; return;
        }
        if (frame.respawn) {
            stopWeaponSound();
            resetMotion();
            doom_pickup_feedback_reset(&pickupFeedback);
            doom_messages_reset(&secretMessages);
            doom_vehicle_feedback_reset(&vehicleFeedback);
            doom_reset(&player, player.doom2);
            deathSoundPlayed=false;
            nativePickupMessages.clear(); nextNativePickupMessage=0;
#if defined(DOOM_MODE_RUNTIME)
            doom_world_runtime_reset(&world, generation);
            worldWarnings = hostWarnings = 0;
            lastSplashSequence=0; splashLogCount=0;
#endif
            doom_engine_reset_damage(engine);
            doom_engine_sync_powerups(engine);
        }
        if (!frame.alive) player.health = 0;
        if (player.health>0) deathSoundPlayed=false;
        else if (!deathSoundPlayed) {
            deathSoundPlayed=true;
            if (!sound("DSPLDETH")) doom_engine_set_voice_replacement(engine,0);
        }
        if (resetCheatInput) { player.cheat_len=0; player.cheat[0]=0; resetCheatInput=false; }
        const uint64_t timestamp = now();
        const unsigned frameElapsed = lastTick && timestamp >= lastTick
            ? static_cast<unsigned>(std::min<uint64_t>(timestamp - lastTick, 250)) : 0;
        auto vehicleRequest=record<DlumaNoRequest>();
        auto vehicle=resultRecord<DlumaVehicleStateResult>();
        char vehicleType[64]{}, vehicleClass[128]{};
        vehicle.vehicle_type.data=vehicleType; vehicle.vehicle_type.capacity=sizeof(vehicleType);
        vehicle.class_name.data=vehicleClass; vehicle.class_name.capacity=sizeof(vehicleClass);
        const DlumaStatus vehicleStatus=playerApi.vehicle_state
            ? playerApi.vehicle_state(playerApi.context,&vehicleRequest,&vehicle) : DLUMA_STATUS_UNAVAILABLE;
        doom_vehicle_feedback_advance(&vehicleFeedback,frameElapsed,vehicleStatus==DLUMA_STATUS_OK,
            vehicle.player_present,vehicle.vehicle_present && vehicle.is_local_driver,vehicleType,vehicle.speed_mps);
        if (frame.carrying) carryFireLatch=true;
        else if (carryFireLatch && !frame.fire_held) carryFireLatch=false;
        combatSuspended=doom_vehicle_feedback_blocks_weapons(&vehicleFeedback) ||
            frame.parkour==2 || frame.interaction_active || handBusy || frame.carrying || carryFireLatch || frame.chat_view;
        if (frame.chat_view) { player.cheat_len=0; player.cheat[0]=0; }
        const bool controllerEligible=frame.alive && player.health>0 && !frame.doom_dead &&
            !frame.death_commit && !combatSuspended;
        const DoomGamepadState controller=doom_gamepad_sample(gamepad,controllerEligible,
            settings.controllerFire=="left",settings.controllerCycle=="reversed");
        if(controllerEligible) frame.fire=frame.fire || controller.fire;
        if (combatSuspended) { doom_cancel_attack(&player); stopWeaponSound(); }
        if (player.health > 0 && !frame.doom_dead) {
            if (!combatSuspended) {
                if(controller.cycle) {
                    static const DoomWeapon order[]={DOOM_FIST,DOOM_CHAINSAW,DOOM_PISTOL,DOOM_SHOTGUN,
                        DOOM_SUPER_SHOTGUN,DOOM_CHAINGUN,DOOM_ROCKET,DOOM_PLASMA,DOOM_BFG};
                    const int current=player.pending_weapon>=0 ? player.pending_weapon : player.weapon;
                    int index=0;
                    for(int i=0;i<DOOM_WEAPON_COUNT;i++) if(order[i]==current) {index=i;break;}
                    for(int i=0;i<DOOM_WEAPON_COUNT;i++) {
                        index=(index+controller.cycle+DOOM_WEAPON_COUNT)%DOOM_WEAPON_COUNT;
                        DoomPlayer probe=player; probe.pending_weapon=-1;
                        doom_select_weapon(&probe,order[index]);
                        if(probe.pending_weapon==order[index]) {doom_select_weapon(&player,order[index]);break;}
                    }
                }
                if (frame.weapon_select >= 0 && frame.weapon_select < DOOM_WEAPON_COUNT)
                    doom_select_weapon(&player, static_cast<DoomWeapon>(frame.weapon_select));
                if (frame.weapon_slot >= 1 && frame.weapon_slot <= 7)
                    doom_select_slot(&player, frame.weapon_slot);
            }
            if (settings.cheats && !frame.chat_view) for (unsigned i = 0; i < std::min(frame.cheat_count, 16u); ++i)
                doom_cheat_char(&player, frame.cheat_chars[i], onDoomEvent, this);
            if (frame.interact_failed) sound("DSNOWAY");
        }
        const float speed=movementSpeed(timestamp,player.health>0 && !frame.doom_dead);
#if defined(DOOM_MODE_LAB)
        if (timestamp>=nextMovementSample && movementSampleCount<1800) {
            DoomMovementSnapshot sample{};
            const int status=doom_engine_sample_movement(engine,&sample);
            if (status==1) {
                char message[384]{};
                std::snprintf(message,sizeof(message),
                    "Doom lab movement query: ms=%llu qpc=%llu id=%d position=%.4f,%.4f,%.4f wanted=%.4f,%.4f,%.4f accel=%.4f decel=%.4f in_air=%d move=%u parkour=%d thread=%u",
                    static_cast<unsigned long long>(timestamp),static_cast<unsigned long long>(sample.qpc),sample.actor_id,
                    static_cast<double>(sample.position[0]),static_cast<double>(sample.position[1]),static_cast<double>(sample.position[2]),
                    static_cast<double>(sample.wanted_world_speed[0]),static_cast<double>(sample.wanted_world_speed[1]),static_cast<double>(sample.wanted_world_speed[2]),
                    static_cast<double>(sample.walk_acceleration),static_cast<double>(sample.walk_deceleration),sample.in_air,
                    sample.move_state,sample.parkour,sample.thread_id);
                log("INFO",message); ++movementSampleCount;
            } else if (status<0 && status!=lastMovementError) {
                char message[96]{};
                std::snprintf(message,sizeof(message),"Doom lab movement query unavailable: stage=%d",-status);
                log("INFO",message);
            }
            lastMovementError=status<0 ? status : 0;
            nextMovementSample=timestamp+100;
        }
#endif
        doom_pickup_feedback_begin_frame(&pickupFeedback, frameElapsed);
        doom_messages_tick(&secretMessages,frameElapsed);
        pickupSoundQueued = false;
        pickupPowerSoundQueued = false;
#if defined(DOOM_MODE_RUNTIME)
        float lootX=0,lootY=0,lootZ=0;
        const bool lootPositionReady=player.health>0 && doom_engine_position(engine,&lootX,&lootY,&lootZ) &&
            std::isfinite(lootX*32.0f) && std::isfinite(lootY*32.0f) && std::isfinite(lootZ*32.0f);
        const DoomVec3 lootPosition{lootX*32.0f,lootZ*32.0f,lootY*32.0f};
        for (unsigned i=0;i<DOOM_WORLD_MAX_PICKUPS;++i) {
            DoomLootRecord convertedLoot{};
            uint64_t token=0;
            if (!doom_engine_loot_peek(engine,&convertedLoot,&token)) break;
            if (convertedLoot.generation!=generation) {
                if (!doom_engine_loot_consume(engine,token)) break;
                continue;
            }
            if (player.health<=0) break;
            DoomDrop drop=DOOM_DROP_MEDIKIT;
            if (convertedLoot.kind==DOOM_LOOT_AMMO) {
                int best=0;
                for (int a=1;a<4;++a)
                    if (player.ammo[a]*player.max_ammo[best]<player.ammo[best]*player.max_ammo[a]) best=a;
                drop=static_cast<DoomDrop>(best);
            } else if (convertedLoot.kind!=DOOM_LOOT_HEALTH) break;
            const int before=drop<=DOOM_DROP_CELLS ? player.ammo[static_cast<int>(drop)] : player.health;
            DoomPlayer collectedPlayer=player;
            const bool collected=doom_collect(&collectedPlayer,drop)!=0;
            bool freeSlot=false;
            if (!collected && lootPositionReady)
                for (const auto &pickup : world.world.pickups) if (!pickup.active) { freeSlot=true; break; }
            if (!collected && !freeSlot) {
                if (!doom_engine_loot_rotate(engine,token)) break;
                continue;
            }
            if (!doom_engine_loot_consume(engine,token)) break;
            if (collected) {
                player=collectedPlayer;
                const int after=drop<=DOOM_DROP_CELLS ? player.ammo[static_cast<int>(drop)] : player.health;
                onPickup(this,drop,after-before);
            } else {
                (void)doom_world_spawn_pickup(&world.world,drop,lootPosition);
            }
        }
#endif
        DoomNativePickupRecord nativePickups[16]{};
        bool secretSoundQueued=false;
        const unsigned nativeCount=doom_engine_native_pickups(engine,nativePickups,16);
        for (unsigned i=0; i<nativeCount; ++i) {
            if (nativePickups[i].kind==DOOM_NATIVE_PICKUP_SECRET) {
                if (doom_messages_post(&secretMessages,nativePickups[i].text,
                        DOOM_MESSAGE_IMPORTANT,4000)) secretSoundQueued=true;
            } else {
                if (nativePickupMessages.size()==32) nativePickupMessages.pop_front();
                nativePickupMessages.emplace_back(nativePickups[i].text);
            }
        }
        if (!nativePickupMessages.empty() && timestamp>=nextNativePickupMessage &&
                doom_messages_post(&pickupFeedback.messages,nativePickupMessages.front().c_str(),DOOM_MESSAGE_NORMAL,3000)) {
            nativePickupMessages.pop_front();
            nextNativePickupMessage=timestamp+1500;
            pickupSoundQueued=true;
        }
        if (lastTick && timestamp >= lastTick) {
            const unsigned elapsed = static_cast<unsigned>(std::min<uint64_t>(timestamp - lastTick, 250));
#if defined(DOOM_MODE_RUNTIME)
            DoomWorldHost host{};
            DoomWorldFrame worldFrame{};
            worldFrame.fire = player.health > 0 && !frame.doom_dead && !combatSuspended && frame.fire;
            const bool hostBound = doom_engine_world_bind(engine, &host) != 0;
            host.self_push=vehicleStatus==DLUMA_STATUS_OK && vehicle.player_present && !vehicle.vehicle_present &&
                frame.parkour==0 && !combatSuspended;
            float x = 0, y = 0, z = 0;
            if (hostBound && doom_engine_position(engine, &x, &y, &z)) {
                worldFrame.host = &host;
                worldFrame.world_active = 1;
                worldFrame.player_position = {x * 32.0f, z * 32.0f, y * 32.0f};
                worldFrame.player_height = 56.0f;
                DoomVec3 boundsCenter{}; float boundsRadius=0, boundsHeight=0;
                if (doom_engine_player_bounds(engine,&boundsCenter,&boundsRadius,&boundsHeight)) {
                    worldFrame.player_height=boundsHeight;
                } else {
                    DoomVec3 currentEye{}, currentForward{};
                    if (doom_engine_shot_basis(engine, &currentEye, &currentForward)) {
                        const float eyeHeight=currentEye.z-worldFrame.player_position.z;
                        // Reject camera transitions before estimating fallback height.
                        if (std::isfinite(eyeHeight) && eyeHeight>=8.0f && eyeHeight<=160.0f)
                            worldFrame.player_height=std::clamp(eyeHeight/0.90f,16.0f,192.0f);
                    }
                }
                DoomDeathRecord deaths[16]{};
                const unsigned count = doom_engine_death_drain(engine, deaths, 16);
                for (unsigned i = 0; i < count && i < 16; ++i)
                    (void)doom_world_runtime_death(&world, &player, deaths[i].id, deaths[i].position);
                if ((player.weapon==DOOM_ROCKET || player.weapon==DOOM_PLASMA ||
                     player.weapon==DOOM_BFG) && projectileBasis(worldFrame))
                    worldFrame.projectile_supported = doom_engine_lab_projectile_ready(
                        engine, static_cast<DoomWeapon>(player.weapon));
            }
            doom_world_runtime_advance(&world, &player, elapsed, &worldFrame, onDoomEvent, onPickup, this);
            DoomHitscanImpact hitEffects[64]{};
            const unsigned hitCount=doom_engine_drain_impacts(engine,hitEffects,64);
            for (unsigned i=0; i<hitCount; ++i)
                doom_world_add_hit_fx(&world.world,hitEffects[i].kind,hitEffects[i].position);
            DoomImpactSound impacts[128]{};
            const unsigned impactCount=doom_world_drain_impact_sounds(&world.world,impacts,128);
            for (unsigned i=0; i<impactCount; ++i) {
                const float dx=(impacts[i].position.x-worldFrame.player_position.x)/32.0f;
                const float dy=(impacts[i].position.y-worldFrame.player_position.y)/32.0f;
                const float distance=std::sqrt(dx*dx+dy*dy);
                sound(impacts[i].lump,std::clamp((38.0f-distance)/32.0f,0.0f,1.0f));
            }
#if defined(DOOM_MODE_LAB)
            logWorld();
            logSplash();
#endif
            if (hostBound) doom_engine_world_unbind(engine);
            DoomDamageObservation observations[64]{};
            const unsigned observationCount=doom_engine_drain_damage_observations(engine, observations, 64);
#if defined(DOOM_MODE_LAB)
            for (unsigned i=0; i<observationCount; ++i) {
                const int weapon=observations[i].weapon;
                if ((weapon==DOOM_ROCKET || weapon==DOOM_PLASMA || weapon==DOOM_BFG) &&
                    damageLogCount[static_cast<size_t>(weapon)]++ < 16)
                    logDamage(observations[i]);
            }
#else
            (void)observationCount;
#endif
            if (worldDisplay && !doom_world_display_update(worldDisplay, engine, assets.wad.get(),
                                                           &world.world, generation) && !worldDisplayWarned) {
                log("WARN", "Dooming Light: world sprites could not be displayed.");
                worldDisplayWarned = true;
            }
            const unsigned newWarnings = world.flags & ~worldWarnings;
            if (newWarnings & DOOM_WORLD_RUNTIME_UNSUPPORTED_PROJECTILE)
                log("WARN", "Dooming Light: projectile support is unavailable for this Player state.");
            if (newWarnings & DOOM_WORLD_RUNTIME_INVALID_ORIGIN)
                log("WARN", "Dooming Light: projectile aim is unavailable.");
            if (newWarnings & DOOM_WORLD_RUNTIME_PROJECTILE_CAPACITY)
                log("WARN", "Dooming Light: projectile limit reached.");
            if (newWarnings & DOOM_WORLD_RUNTIME_INVALID_PLAYER)
                log("WARN", "Dooming Light: player position is unavailable.");
            worldWarnings |= newWarnings;
            const unsigned newHostWarnings = world.world.errors & ~hostWarnings;
            if (newHostWarnings & DOOM_WORLD_ERROR_HOST_QUERY)
                log("WARN", "Dooming Light: a projectile world query failed.");
            if (newHostWarnings & DOOM_WORLD_ERROR_HOST_DAMAGE)
                log("WARN", "Dooming Light: projectile damage could not be applied.");
            hostWarnings |= newHostWarnings;
#else
            doom_advance(&player, elapsed, player.health > 0 && !frame.doom_dead && !combatSuspended && frame.fire,
                         onDoomEvent, this);
#endif
        }
        lastTick = timestamp;
        if (secretSoundQueued) {
            secretSound();
        } else if (pickupSoundQueued && (!pickupPowerSoundQueued || !sound("DSGETPOW"))) sound("DSITEMUP");
        if (combatSuspended || player.weapon != DOOM_CHAINSAW || player.switch_phase == 1 || player.health <= 0)
            stopWeaponSound();
        DoomPlayer renderPlayer{};
        doom_vehicle_feedback_present(&vehicleFeedback,&player,&renderPlayer);
        if (fallingFace(timestamp,frame,vehicleStatus==DLUMA_STATUS_OK &&
                vehicle.player_present && !vehicle.vehicle_present)) {
            const int tier=(100-std::clamp(player.health,0,100))*5/101;
            renderPlayer.face_index=tier*8+5;
        }
        const bool living=player.health>0 && !frame.doom_dead;
        const int reachSide=living && frame.edge_active && (frame.edge_state==2 || frame.edge_state==3)
            ? (frame.edge_state==2 ? -1 : 1) : 0;
        int handSide=0;
        float handY=0;
        const DoomHudHandResult hand=doom_hud_hand_update(assets.hud.get(),frameElapsed,
            reachSide,living && pickupSoundQueued,0,living && frame.parkour==2,&handSide,&handY);
        const bool pickupGesture=doom_hud_pickup_gesture(assets.hud.get())!=0;
        if (living && (hand!=DOOM_HUD_HAND_WEAPON || frame.interaction_active)) weaponReturning=true;
        float weaponX=0, weaponY=0;
        doom_hud_motion_update(assets.hud.get(),&player,frameElapsed,speed,
            living && (hand!=DOOM_HUD_HAND_WEAPON || frame.interaction_active) ? 200.0f :
                frame.parkour==1 ? 12.0f : 0.0f,&weaponX,&weaponY);
        if (!living || (hand==DOOM_HUD_HAND_WEAPON && !frame.interaction_active &&
                doom_hud_lowering(assets.hud.get())<=1.0f)) weaponReturning=false;
        handBusy=living && (hand!=DOOM_HUD_HAND_WEAPON || weaponReturning);
        if (doom_hud_render_layers(assets.hud.get(), &renderPlayer,
                weaponPixels.data(), weaponPixels.size(), statusPixels.data(), statusPixels.size(),
                messagePixels.data(), messagePixels.size(), doom_pickup_feedback_text(&pickupFeedback))) {
            if (living && hand==DOOM_HUD_HAND_VISIBLE && (!frame.interaction_active || pickupGesture) &&
                    !doom_vehicle_feedback_blocks_weapons(&vehicleFeedback)) {
                if (doom_hud_render_reach(assets.hud.get(),weaponPixels.data(),weaponPixels.size(),handSide)) {
                    weaponX=0.0f; weaponY=handY;
                } else {
                    weaponPixels.fill(0); weaponX=weaponY=0.0f;
                }
            } else if (living && (hand==DOOM_HUD_HAND_HIDDEN ||
                    (hand==DOOM_HUD_HAND_VISIBLE && frame.interaction_active && !pickupGesture))) {
                weaponPixels.fill(0); weaponX=weaponY=0.0f;
            }
            doom_hud_powerup_visuals(assets.hud.get(),&player,weaponPixels.data(),weaponPixels.size());
            const bool chatReady=doom_hud_render_chat(assets.hud.get(),messagePixels.data(),messagePixels.size(),&frame.chat,
                        pickupFeedback.messages.remaining_ms!=0)!=0;
            const bool presented=doom_engine_present_layers(engine, weaponPixels.data(), statusPixels.data(),
                messagePixels.data(), settings.hudFit ? 0.f : settings.hud / 100.f,
                settings.weaponScale / 100.f,weaponX,weaponY)!=0;
            doom_chat_presented(chatReady && presented);
            presentSecret(presented);
            doom_engine_allow_native_pickups(engine,presented && living);
        } else { doom_chat_presented(0); presentSecret(false); }
        musicTick();
    }
    DlumaStatus prepare(const DlumaApiV1* host) {
        if (!host || host->struct_size < sizeof(*host) || host->abi_version != DLUMA_API_ABI_VERSION ||
            host->api_major != 1 || !host->get_service) return DLUMA_STATUS_INVALID_ARGUMENT;
        api = host;
        return service(DLUMA_SERVICE_CORE_V1, core) ? DLUMA_STATUS_OK : DLUMA_STATUS_UNAVAILABLE;
    }
    DlumaStatus start() {
        if (started) return DLUMA_STATUS_OK;
        if (!service(DLUMA_SERVICE_EVENTS_V1, events) || !events.subscribe_update ||
            !service(DLUMA_SERVICE_CONFIGURATION_V1, config)) return DLUMA_STATUS_UNAVAILABLE;
        (void)service(DLUMA_SERVICE_AUDIO_V1, media);
        if (service(DLUMA_SERVICE_PLAYER_V1, playerApi)) (void)doom_movement_init(&movement, &playerApi);
        engine = doom_engine_create(); audio = doom_audio_create(); gamepad=doom_gamepad_create();
        bundledSecret=bundledFile(L"dssecret.wav");
        bundleDirectory=bundledFile(L"");
        bundledSoundFont=bundledFile(L"TimGM6mb.sf2");
        if (!engine || !audio) return DLUMA_STATUS_UNAVAILABLE;
#if defined(DOOM_MODE_RUNTIME)
        worldDisplay = doom_world_display_create();
#endif
#if defined(DOOM_MODE_LAB)
        const char *worldTrace = std::getenv("DOOMING_LIGHT_WORLD_TRACE");
        traceWorld=worldTrace && std::strcmp(worldTrace,"1")==0;
#endif
        doom_engine_bind(engine, &player, onDoomEvent, this);
        (void)doom_engine_initialize(engine);
        configuration();
        auto request = record<DlumaEventSubscribeRequest>();
        auto result = resultRecord<DlumaTokenResult>();
        request.callback_id = UpdateCallback;
        if (events.subscribe_update(events.context, &request, &result) != DLUMA_STATUS_OK) return DLUMA_STATUS_UNAVAILABLE;
        updateToken = result.token;
        if (!updateToken) return DLUMA_STATUS_UNAVAILABLE;
        if (config.subscribe_changed) {
            request.callback_id = ConfigCallback; result = resultRecord<DlumaTokenResult>();
            if (config.subscribe_changed(config.context, &request, &result) == DLUMA_STATUS_OK) configToken = result.token;
        }
        started = true;
        return DLUMA_STATUS_OK;
    }
    void stop() {
        scope = false; deactivate();
        cancel(updateToken); cancel(configToken);
        if (rendering.valid()) rendering.wait();
        if (loading.valid()) loading.wait();
        if (secretLoading.valid()) secretLoading.wait();
        doom_gamepad_destroy(gamepad); gamepad=nullptr;
        rendering = {}; loading = {};
        secretLoading={}; secretEffect={}; secretRequested.clear();
        assets.hud.reset(); assets.otherMusicWad.reset(); assets.wad.reset();
        assets.musicChoices.clear();
#if defined(DOOM_MODE_RUNTIME)
        doom_world_display_destroy(worldDisplay); worldDisplay = nullptr;
        worldDisplayWarned = false;
#endif
        doom_engine_destroy(engine); engine = nullptr;
        doom_audio_destroy(audio); audio = nullptr;
        started = false; api = nullptr;
    }
};
Runtime runtime;
DlumaStatus DLUMA_API_CALL preGame(const DlumaApiV1* api) { return runtime.prepare(api); }
DlumaStatus DLUMA_API_CALL postGame(const DlumaApiV1*) {
    try { const auto status = runtime.start(); if (status != DLUMA_STATUS_OK) runtime.stop(); return status; }
    catch (...) { runtime.stop(); return DLUMA_STATUS_INTERNAL_ERROR; }
}
DlumaStatus DLUMA_API_CALL activated(const DlumaApiV1*, const DlumaScopeActivationV1* activation) {
    if (!activation || activation->struct_size < sizeof(*activation)) return DLUMA_STATUS_INVALID_ARGUMENT;
    runtime.scope = true; runtime.haveGeneration = false;
    return DLUMA_STATUS_OK;
}
void DLUMA_API_CALL deactivated(const DlumaApiV1*, const DlumaScopeActivationV1*) {
    runtime.scope = false;
    EngineUpdateScope callback(runtime.engine);
    runtime.deactivate();
}
void DLUMA_API_CALL shutdown(const DlumaApiV1*, uint32_t) { runtime.stop(); }
DlumaStatus DLUMA_API_CALL onEvent(const DlumaApiV1*, const DlumaModEventV1* event) {
    if (!event || event->struct_size < sizeof(*event)) return DLUMA_STATUS_INVALID_ARGUMENT;
    try {
        if (runtime.started && event->kind == DLUMA_MOD_EVENT_CONFIGURATION_CHANGED && event->callback_id == ConfigCallback)
            runtime.configuration();
        if (runtime.started && event->kind == DLUMA_MOD_EVENT_UPDATE && event->callback_id == UpdateCallback) {
            EngineUpdateScope callback(runtime.engine);
            if (!callback) return DLUMA_STATUS_UNAVAILABLE;
            runtime.tick(event->map_generation);
        }
        return DLUMA_STATUS_OK;
    } catch (...) {
        EngineUpdateScope cleanup(runtime.engine);
        runtime.deactivate();
        runtime.log("ERROR", "Dooming Light stopped after a resource error.");
        return DLUMA_STATUS_INTERNAL_ERROR;
    }
}
const DlumaNativeModApiV2 callbacks = {sizeof(DlumaNativeModApiV2), DLUMA_NATIVE_MOD_ABI_V2,
    preGame, postGame, activated, deactivated, shutdown, onEvent, nullptr};
}
extern "C" DLUMA_API_EXPORT const DlumaNativeModApiV2* DLUMA_API_CALL DLUMANativeMod_GetApiV2() { return &callbacks; }
