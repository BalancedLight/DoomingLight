#define _CRT_SECURE_NO_WARNINGS
#include "dluma_api.h"
#include "doom_audio.h"
#include "doom_core.h"
#include "doom_engine.h"
#include "doom_hud.h"
#include "doom_music.h"
#include "doom_movement.h"
#include "doom_pickup_feedback.h"
#include "doom_synth.h"
#include "doom_wad.h"
#include "doom_world_display.h"
#include "doom_world_runtime.h"
#include "doom_vehicle_feedback.h"
#include "wad_discovery.h"
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
#include <string>
#include <vector>

namespace {
constexpr uint64_t UpdateCallback = 1, ConfigCallback = 2;
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
    HudPtr hud;
};
struct PreparedMusic {
    std::unique_ptr<int16_t, PcmDeleter> pcm;
    size_t frames = 0;
    unsigned track = 0;
    uint64_t epoch = 0;
    bool soundfontFallback = false;
    bool soundfontUsed = false;
};
struct Settings {
    std::string game = "auto", doom, doom2, music = "doom", musicSynth = "opl", soundfont;
    bool classic = false, cheats = true, hudFit = true, bilinear = false, dynamicLights = true;
    int track = 0, hud = 100, weaponScale = 100, incoming = 100, outgoing = 100;
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
    Settings settings;
    DoomPlayer player{};
    DoomEngine* engine = nullptr;
    DoomAudio* audio = nullptr;
    Assets assets;
    std::future<Assets> loading;
    std::future<PreparedMusic> rendering;
    PreparedMusic pendingMusic;
    std::shared_ptr<std::atomic<bool>> cancelRender;
    std::array<uint8_t, 320 * 200 * 4> weaponPixels{};
    std::array<uint8_t, 320 * 32 * 4> statusPixels{};
    std::array<uint8_t, 320 * 16 * 4> messagePixels{};
    DoomPickupFeedback pickupFeedback{};
    DoomVehicleFeedback vehicleFeedback{};
    bool pickupSoundQueued = false;
    bool combatSuspended = false, handBusy = false, weaponReturning = false;
    bool deathSoundPlayed = false;
    std::deque<std::string> nativePickupMessages;
    uint64_t nextNativePickupMessage = 0;
    uint64_t flyHudTick = 0;
    uint64_t sawStartupUntil = 0;
    uint64_t updateToken = 0, configToken = 0, musicToken = 0;
    uint64_t lastTick = 0, retryMusicAt = 0;
    uint64_t musicEpoch = 0;
    uint64_t motionSampleAt = 0;
    unsigned lastDamageSequence = 0;
    float motionX = 0.0f, motionZ = 0.0f, lastMotionSpeed = 0.0f;
#if defined(DOOM_MODE_LAB)
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
    DoomNoclipDiagnostic lastNoclipDiagnostic{};
    unsigned noclipDiagnosticLogs = 0;
    std::array<unsigned, DOOM_WORLD_MAX_PROJECTILES> tracedAge{};
    std::array<int, DOOM_WORLD_MAX_PROJECTILES> tracedState{};
#endif
    uint32_t generation = 0;
    unsigned requestedTrack = 0, playingTrack = 0, resumeTrack = 0;
    bool scope = false, active = false, started = false, haveGeneration = false;
    bool resetCheatInput = false;
    bool reloadAssets = false, musicPlaying = false, musicOverride = false, requestOverride = false;
    bool bindingWarning = false;

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
        const std::string oldSynth = settings.musicSynth, oldFont = settings.soundfont;
        const bool oldCheats = settings.cheats;
        settings.game = text("game", "auto");
        settings.doom = text("doom_wad", ""); settings.doom2 = text("doom2_wad", "");
        settings.music = text("music", "doom");
        settings.musicSynth = text("music_synth", "opl");
        settings.soundfont = text("soundfont", "");
        settings.classic = text("movement", "traversal") == "classic";
        settings.cheats = integer("cheats", 1, 0, 1) != 0;
        if (oldCheats!=settings.cheats) resetCheatInput=true;
        settings.track = integer("default_track", 0, 0, 49);
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
            if (active && settings.music == "doom") requestMusic(defaultTrack());
        } else if ((oldSynth!=settings.musicSynth || oldFont!=settings.soundfont) && active &&
                   (settings.music=="doom" || musicOverride || requestOverride)) {
            ++musicEpoch;
            if (cancelRender) cancelRender->store(true);
            pendingMusic={};
            requestMusic(requestedTrack ? requestedTrack : defaultTrack(),musicOverride || requestOverride);
        }
    }
    unsigned defaultTrack() const {
        return settings.track ? static_cast<unsigned>(settings.track) : player.doom2 ? 1u : 11u;
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
            if (wasPlaying) resumeTrack = playingTrack;
            musicPlaying = false;
            (void)releaseMusicLease();
            if (musicToken) return false;
            if (wasPlaying && !pendingMusic.pcm && !rendering.valid()) requestMusic(resumeTrack, musicOverride);
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
    void requestMusic(unsigned track, bool overrideRequest = false) {
        if (!assets.wad) return;
        char name[9]{};
        DoomLump music{}, bank{};
        if (!doom_music_lump_name(player.doom2, track, name) ||
            !doom_wad_lump(assets.wad.get(), name, &music) ||
            !doom_wad_lump(assets.wad.get(), "GENMIDI", &bank) ||
            music.size > 4u * 1024u * 1024u || bank.size > 65536u) return;
        requestedTrack = track;
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
        const std::string fontPath=settings.soundfont;
        rendering = std::async(std::launch::async,
            [score = std::move(score), instruments = std::move(instruments), track, epoch, cancel,
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
            if (result.track != requestedTrack || result.epoch != musicEpoch) { requestMusic(requestedTrack, requestOverride); return; }
            if (result.soundfontFallback)
                log("WARN","Dooming Light could not load the SoundFont; using Classic OPL.");
            if (!result.pcm) {
                log("WARN", "Dooming Light could not prepare the selected music track.");
                requestOverride = false;
                if (!musicPlaying && resumeTrack) {
                    const unsigned previous = resumeTrack;
                    resumeTrack = 0;
                    requestMusic(previous, musicOverride);
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
                resumeTrack = 0;
                if (requestOverride) musicOverride = true;
                requestOverride = false;
            }
            else {
                requestOverride = false;
                if (!musicPlaying && resumeTrack) {
                    const unsigned previous = resumeTrack;
                    resumeTrack = 0;
                    requestMusic(previous, musicOverride);
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
        doom_hud_motion_reset(assets.hud.get());
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
#if defined(DOOM_MODE_LAB)
    bool projectileBasis(DoomWorldFrame& frame) const {
        DoomVec3 eye{}, forward{};
        if (!frame.host || !frame.host->aim || !frame.host->sweep ||
            !doom_engine_shot_basis(engine, &eye, &forward)) return false;
        DoomTrace aim{}, clearance{};
        if (!frame.host->aim(frame.host->context, eye, forward, 2048.0f, &aim)) return false;
        DoomVec3 muzzle{frame.player_position.x, frame.player_position.y,
                        frame.player_position.z + 32.0f};
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
            const char *message=nullptr;
            switch (event->value) {
            case DOOM_CHEAT_GOD:
                message=self.player.god ? "Degreelessness Mode On" : "Degreelessness Mode Off";
                break;
            case DOOM_CHEAT_AMMO: message="Ammo (no keys) Added"; break;
            case DOOM_CHEAT_AMMO_KEYS: message="Very Happy Ammo Added"; break;
            case DOOM_CHEAT_CHOPPERS: message="... doesn't suck - GM"; break;
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
#if defined(DOOM_MODE_LAB)
    static void onPickup(void* context, DoomDrop kind, int amount) {
        Runtime& self = *static_cast<Runtime*>(context);
        doom_pickup_feedback_record(&self.pickupFeedback, kind, amount);
        if (amount > 0) self.pickupSoundQueued = true;
    }
#endif
    void beginAssets() {
        if (loading.valid()) { reloadAssets = true; return; }
        const Settings selected = settings;
        loading = std::async(std::launch::async, [selected]() {
            Assets result;
            char path[4097]{};
            bool found = false;
            if (selected.game != "doom")
                found = doom_find_iwad(2, selected.doom2.c_str(), path, sizeof(path)) != 0;
            if (!found && selected.game != "doom2")
                found = doom_find_iwad(1, selected.doom.c_str(), path, sizeof(path)) != 0;
            if (found) {
                result.wad.reset(doom_wad_open(path));
                if (result.wad) result.hud.reset(doom_hud_create(result.wad.get()));
            }
            return result;
        });
    }
    void deactivate() {
        active = false;
        combatSuspended=false; handBusy=false; weaponReturning=false;
        deathSoundPlayed=false;
        nativePickupMessages.clear(); nextNativePickupMessage=0;
        flyHudTick=0;
        lastDamageSequence=0;
        resetMotion();
        doom_pickup_feedback_reset(&pickupFeedback);
        doom_vehicle_feedback_reset(&vehicleFeedback);
        pickupSoundQueued = false;
        sawStartupUntil = 0;
#if defined(DOOM_MODE_LAB)
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
            beginAssets();
        }
        if (loading.valid() && loading.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            Assets loaded = loading.get();
            if (reloadAssets) { reloadAssets = false; beginAssets(); return; }
            assets = std::move(loaded);
            if (!assets.wad || !assets.hud) { log("WARN", "Dooming Light needs a valid Doom or Doom II WAD. Set its path in Mods."); return; }
            doom_reset(&player, doom_wad_is_doom2(assets.wad.get()));
#if defined(DOOM_MODE_LAB)
            doom_world_runtime_reset(&world, generation);
#endif
            doom_engine_reset_damage(engine);
            DoomSound pain{},jump{},death{};
            const bool painReady=audio && doom_wad_sound(assets.wad.get(),"DSPLPAIN",&pain) &&
                doom_wad_sound(assets.wad.get(),"DSOOF",&jump) &&
                doom_wad_sound(assets.wad.get(),"DSPLDETH",&death);
            doom_sound_free(&pain); doom_sound_free(&jump); doom_sound_free(&death);
            doom_engine_set_voice_replacement(engine,painReady);
            if (!doom_engine_activate(engine, generation)) {
                if (!bindingWarning) {
                    log("WARN", "Dooming Light is unavailable for this Player build.");
                    bindingWarning = true;
                }
                return;
            }
            active = true;
            if (settings.music == "doom") requestMusic(defaultTrack());
        }
        if (!active) { (void)releaseMusicLease(); return; }
        DoomEngineFrame frame{};
        if (!doom_engine_poll(engine, &frame) || !frame.ready) { deactivate(); return; }
        doom_engine_allow_native_pickups(engine,0);
        doom_audio_poll(audio);
        doom_audio_pause(audio, frame.paused && !frame.noclip_input);
#if defined(DOOM_MODE_LAB)
        if (noclipDiagnosticLogs<64 && (!noclipDiagnosticLogs ||
                std::memcmp(&lastNoclipDiagnostic,&frame.noclip_diagnostic,sizeof(lastNoclipDiagnostic)))) {
            const auto &d=frame.noclip_diagnostic;
            char message[256]{};
            std::snprintf(message,sizeof(message),
                "Doom lab noclip: enabled=%d tracked=%d validation=%d camera=%d saved=%d owner=%d actor=%d input=%d transition=%d",
                d.enabled,d.tracked,d.validation,d.camera_mode,d.saved,d.owner,d.actor,d.input_mode,d.last_transition);
            log("INFO",message); lastNoclipDiagnostic=d; ++noclipDiagnosticLogs;
        }
        for (unsigned i=0; i<frame.voice_observation_count && i<64; ++i) {
            const auto &v=frame.voice_observations[i];
            char message[128]{};
            std::snprintf(message,sizeof(message),"Doom lab voice: kind=%u category=%u caller=%08X",v.kind,v.category,v.caller_rva);
            log("INFO",message);
        }
#endif
        if (frame.noclip_notice) {
            const char *message=frame.noclip_notice==1 ? "No Clipping Mode ON" :
                frame.noclip_notice==2 ? "No Clipping Mode OFF" : "Noclip Unavailable";
            (void)doom_messages_post(&pickupFeedback.messages,message,DOOM_MESSAGE_IMPORTANT,4000);
        }
        if (frame.noclip) {
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
                if (settings.cheats)
                    for (unsigned i=0; i<std::min(frame.cheat_count,16u); ++i)
                        doom_cheat_char(&player,frame.cheat_chars[i],onDoomEvent,this);
                if (doom_hud_render_layers(assets.hud.get(),&player,
                        weaponPixels.data(),weaponPixels.size(),statusPixels.data(),statusPixels.size(),
                        messagePixels.data(),messagePixels.size(),doom_pickup_feedback_text(&pickupFeedback))) {
                    weaponPixels.fill(0);
                    (void)doom_engine_present_layers(engine,weaponPixels.data(),statusPixels.data(),messagePixels.data(),
                        settings.hudFit ? 0.f : settings.hud/100.f,settings.weaponScale/100.f,0,0);
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
        if (frame.paused) { stopWeaponSound(); resetMotion(); lastTick = 0; return; }
        if (frame.respawn) {
            stopWeaponSound();
            resetMotion();
            doom_pickup_feedback_reset(&pickupFeedback);
            doom_vehicle_feedback_reset(&vehicleFeedback);
            doom_reset(&player, player.doom2);
            deathSoundPlayed=false;
            nativePickupMessages.clear(); nextNativePickupMessage=0;
#if defined(DOOM_MODE_LAB)
            doom_world_runtime_reset(&world, generation);
            worldWarnings = hostWarnings = 0;
            lastSplashSequence=0; splashLogCount=0;
#endif
            doom_engine_reset_damage(engine);
            musicOverride = false;
            if (settings.music == "doom") requestMusic(defaultTrack());
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
        combatSuspended=doom_vehicle_feedback_blocks_weapons(&vehicleFeedback) ||
            frame.parkour==2 || frame.interaction_active || handBusy;
        if (combatSuspended) { doom_cancel_attack(&player); stopWeaponSound(); }
        if (player.health > 0 && !frame.doom_dead) {
            if (!combatSuspended) {
                if (frame.weapon_select >= 0 && frame.weapon_select < DOOM_WEAPON_COUNT)
                    doom_select_weapon(&player, static_cast<DoomWeapon>(frame.weapon_select));
                if (frame.weapon_slot >= 1 && frame.weapon_slot <= 7)
                    doom_select_slot(&player, frame.weapon_slot);
            }
            if (settings.cheats) for (unsigned i = 0; i < std::min(frame.cheat_count, 16u); ++i)
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
        pickupSoundQueued = false;
        DoomNativePickupRecord nativePickups[16]{};
        const unsigned nativeCapacity=nativePickupMessages.size()<32
            ? static_cast<unsigned>(std::min<size_t>(16,32-nativePickupMessages.size())) : 0;
        const unsigned nativeCount=doom_engine_native_pickups(engine,nativePickups,nativeCapacity);
        for (unsigned i=0; i<nativeCount; ++i) nativePickupMessages.emplace_back(nativePickups[i].text);
        if (!nativePickupMessages.empty() && timestamp>=nextNativePickupMessage &&
                doom_messages_post(&pickupFeedback.messages,nativePickupMessages.front().c_str(),DOOM_MESSAGE_NORMAL,3000)) {
            nativePickupMessages.pop_front();
            nextNativePickupMessage=timestamp+1500;
            pickupSoundQueued=true;
        }
        if (lastTick && timestamp >= lastTick) {
            const unsigned elapsed = static_cast<unsigned>(std::min<uint64_t>(timestamp - lastTick, 250));
#if defined(DOOM_MODE_LAB)
            DoomWorldHost host{};
            DoomWorldFrame worldFrame{};
            worldFrame.fire = player.health > 0 && !frame.doom_dead && !combatSuspended && frame.fire;
            const bool hostBound = doom_engine_world_bind(engine, &host) != 0;
            float x = 0, y = 0, z = 0;
            if (hostBound && doom_engine_position(engine, &x, &y, &z)) {
                worldFrame.host = &host;
                worldFrame.world_active = 1;
                worldFrame.player_position = {x * 32.0f, z * 32.0f, y * 32.0f};
                worldFrame.player_height = 56.0f;
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
            logWorld();
            logSplash();
            if (hostBound) doom_engine_world_unbind(engine);
            DoomDamageObservation observations[64]{};
            const unsigned observationCount=doom_engine_drain_damage_observations(engine, observations, 64);
            for (unsigned i=0; i<observationCount; ++i) {
                const int weapon=observations[i].weapon;
                if ((weapon==DOOM_ROCKET || weapon==DOOM_PLASMA || weapon==DOOM_BFG) &&
                    damageLogCount[static_cast<size_t>(weapon)]++ < 16)
                    logDamage(observations[i]);
            }
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
        if (pickupSoundQueued) sound("DSITEMUP");
        if (combatSuspended || player.weapon != DOOM_CHAINSAW || player.switch_phase == 1 || player.health <= 0)
            stopWeaponSound();
        DoomPlayer renderPlayer{};
        doom_vehicle_feedback_present(&vehicleFeedback,&player,&renderPlayer);
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
            const bool presented=doom_engine_present_layers(engine, weaponPixels.data(), statusPixels.data(),
                messagePixels.data(), settings.hudFit ? 0.f : settings.hud / 100.f,
                settings.weaponScale / 100.f,weaponX,weaponY)!=0;
            const bool important=pickupFeedback.messages.remaining_ms &&
                pickupFeedback.messages.priority==DOOM_MESSAGE_IMPORTANT;
            doom_engine_allow_native_pickups(engine,presented && living && !important && nativePickupMessages.size()<=16);
        }
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
        engine = doom_engine_create(); audio = doom_audio_create();
        if (!engine || !audio) return DLUMA_STATUS_UNAVAILABLE;
#if defined(DOOM_MODE_LAB)
        const char *worldSprites = std::getenv("DOOMING_LIGHT_WORLD_SPRITES");
        if (worldSprites && std::strcmp(worldSprites, "1") == 0)
            worldDisplay = doom_world_display_create();
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
        rendering = {}; loading = {};
        assets.hud.reset(); assets.wad.reset();
#if defined(DOOM_MODE_LAB)
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
