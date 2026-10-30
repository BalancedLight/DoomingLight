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
#include "wad_discovery.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
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
};
struct Settings {
    std::string game = "auto", doom, doom2, music = "doom";
    bool classic = false, cheats = true, hudFit = true, bilinear = false;
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
    bool pickupSoundQueued = false;
    uint64_t sawStartupUntil = 0;
    uint64_t updateToken = 0, configToken = 0, musicToken = 0;
    uint64_t lastTick = 0, retryMusicAt = 0;
    uint64_t musicEpoch = 0;
#if defined(DOOM_MODE_LAB)
    uint64_t nextPositionLog = 0;
    std::array<unsigned, DOOM_WEAPON_COUNT> attackLogCount{};
    std::array<unsigned, DOOM_WEAPON_COUNT> damageLogCount{};
    unsigned inputLogCount = 0;
    int previousFire = -1, previousPaused = -1;
    DoomWorldRuntime world{};
    DoomWorldDisplay *worldDisplay = nullptr;
    unsigned worldWarnings = 0, hostWarnings = 0;
    bool worldDisplayWarned = false;
    bool traceWorld = false;
    unsigned worldTraceCount = 0;
    std::array<unsigned, DOOM_WORLD_MAX_PROJECTILES> tracedAge{};
    std::array<int, DOOM_WORLD_MAX_PROJECTILES> tracedState{};
#endif
    uint32_t generation = 0;
    unsigned requestedTrack = 0, playingTrack = 0, resumeTrack = 0;
    bool scope = false, active = false, started = false, haveGeneration = false;
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
        settings.game = text("game", "auto");
        settings.doom = text("doom_wad", ""); settings.doom2 = text("doom2_wad", "");
        settings.music = text("music", "doom");
        settings.classic = text("movement", "traversal") == "classic";
        settings.cheats = integer("cheats", 1, 0, 1) != 0;
        settings.track = integer("default_track", 0, 0, 49);
        settings.hud = integer("hud_scale", 100, 50, 200);
        settings.hudFit = integer("hud_fit_screen", 1, 0, 1) != 0;
        settings.weaponScale = integer("weapon_scale", 100, 50, 200);
        settings.bilinear = integer("bilinear_filtering", 0, 0, 1) != 0;
        settings.incoming = integer("incoming_damage", 100, 10, 500);
        settings.outgoing = integer("outgoing_damage", 100, 10, 500);
        settings.musicVolume = integer("music_volume", 70, 0, 100);
        settings.effectsVolume = integer("effects_volume", 100, 0, 100);
        doom_engine_configure(engine, settings.classic, settings.incoming / 100.f,
            settings.outgoing / 100.f);
        doom_engine_set_texture_filtering(engine, settings.bilinear);
        doom_engine_set_fire_key(engine, integer("fire_key", 162, 1, 254));
        doom_audio_music_volume(audio, settings.musicVolume / 100.f);
        (void)doom_audio_weapon_volume(audio, settings.effectsVolume / 100.f);
        if (oldMusic != settings.music) {
            musicOverride = false;
            stopMusic();
            if (active && settings.music == "doom") requestMusic(defaultTrack());
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
        rendering = std::async(std::launch::async,
            [score = std::move(score), instruments = std::move(instruments), track, epoch, cancel]() {
                PreparedMusic output;
                int16_t* pcm = nullptr;
                auto canceled = [](void* context) -> int {
                    return static_cast<std::atomic<bool>*>(context)->load() ? 1 : 0;
                };
                if (doom_synth_render_cancelable(score.data(), score.size(), instruments.data(),
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
    void sound(const char* name) {
        DoomSound effect{};
        if (audio && assets.wad && doom_wad_sound(assets.wad.get(), name, &effect)) {
            (void)doom_audio_play_pcm(audio, effect.samples, effect.count, effect.rate,
                settings.effectsVolume / 100.f);
            doom_sound_free(&effect);
        }
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
    void logDamage(const DoomDamageObservation& observation) const {
        char before[32] = "unavailable", after[32] = "unavailable", message[224]{};
        if (observation.before_valid)
            std::snprintf(before, sizeof(before), "%.3f", static_cast<double>(observation.before));
        if (observation.after_valid)
            std::snprintf(after, sizeof(after), "%.3f", static_cast<double>(observation.after));
        std::snprintf(message, sizeof(message),
            "Doom lab damage: weapon=%d target=%d native=%.3f before=%s after=%s removed=%d",
            observation.weapon, observation.target_id, static_cast<double>(observation.native_damage),
            before, after, observation.removed);
        log("INFO", message);
    }
#endif
    static void onDoomEvent(void* context, const DoomEvent* event) {
        Runtime& self = *static_cast<Runtime*>(context);
        if (!event) return;
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
        else if (event->type == DOOM_EVENT_CHEAT) doom_engine_reset_damage(self.engine);
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
                self.requestMusic(static_cast<unsigned>(event->value), true);
            }
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
        doom_pickup_feedback_reset(&pickupFeedback);
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
            worldTraceCount=0; tracedAge.fill(0); tracedState.fill(DOOM_PROJECTILE_FREE);
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
        doom_audio_poll(audio);
        doom_audio_pause(audio, frame.paused);
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
        if (frame.paused) { stopWeaponSound(); lastTick = 0; return; }
        if (frame.respawn) {
            stopWeaponSound();
            doom_pickup_feedback_reset(&pickupFeedback);
            doom_reset(&player, player.doom2);
#if defined(DOOM_MODE_LAB)
            doom_world_runtime_reset(&world, generation);
            worldWarnings = hostWarnings = 0;
#endif
            doom_engine_reset_damage(engine);
            musicOverride = false;
            if (settings.music == "doom") requestMusic(defaultTrack());
        }
        if (!frame.alive) player.health = 0;
        if (player.health > 0 && !frame.doom_dead) {
            if (frame.weapon_select >= 0 && frame.weapon_select < DOOM_WEAPON_COUNT)
                doom_select_weapon(&player, static_cast<DoomWeapon>(frame.weapon_select));
            if (frame.weapon_slot >= 1 && frame.weapon_slot <= 7)
                doom_select_slot(&player, frame.weapon_slot);
            if (settings.cheats) for (unsigned i = 0; i < std::min(frame.cheat_count, 16u); ++i)
                doom_cheat_char(&player, frame.cheat_chars[i], onDoomEvent, this);
            if (frame.interact_failed) sound("DSNOWAY");
        }
        const uint64_t timestamp = now();
        const unsigned frameElapsed = lastTick && timestamp >= lastTick
            ? static_cast<unsigned>(std::min<uint64_t>(timestamp - lastTick, 250)) : 0;
        doom_pickup_feedback_begin_frame(&pickupFeedback, frameElapsed);
        pickupSoundQueued = false;
        if (lastTick && timestamp >= lastTick) {
            const unsigned elapsed = static_cast<unsigned>(std::min<uint64_t>(timestamp - lastTick, 250));
#if defined(DOOM_MODE_LAB)
            DoomWorldHost host{};
            DoomWorldFrame worldFrame{};
            worldFrame.fire = player.health > 0 && !frame.doom_dead && frame.fire;
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
            logWorld();
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
            doom_advance(&player, elapsed, player.health > 0 && !frame.doom_dead && frame.fire,
                         onDoomEvent, this);
#endif
        }
        lastTick = timestamp;
        if (pickupSoundQueued) sound("DSITEMUP");
        if (player.weapon != DOOM_CHAINSAW || player.switch_phase == 1 || player.health <= 0)
            stopWeaponSound();
        if (doom_hud_render_layers(assets.hud.get(), &player,
                weaponPixels.data(), weaponPixels.size(), statusPixels.data(), statusPixels.size(),
                messagePixels.data(), messagePixels.size(), doom_pickup_feedback_text(&pickupFeedback)))
            (void)doom_engine_present_layers(engine, weaponPixels.data(), statusPixels.data(),
                messagePixels.data(), settings.hudFit ? 0.f : settings.hud / 100.f, settings.weaponScale / 100.f);
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
