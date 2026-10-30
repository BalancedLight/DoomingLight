#include "doom_audio.h"
#include <windows.h>
#include <xaudio2.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <new>

struct DoomVoice {
    IXAudio2SourceVoice *voice;
    int16_t *samples;
};
struct DoomAudio {
    IXAudio2 *engine;
    IXAudio2MasteringVoice *master;
    DoomVoice voices[16];
    DoomVoice music;
    DoomVoice weapon;
    unsigned weaponClip;
    bool uninitialize;
    bool paused;
    DWORD comThread;
};

static void clear_voice(DoomVoice &slot) {
    if (slot.voice) { slot.voice->Stop(); slot.voice->DestroyVoice(); }
    std::free(slot.samples);
    slot = {};
}

extern "C" DoomAudio *doom_audio_create(void) {
    DoomAudio *audio = new (std::nothrow) DoomAudio{};
    if (!audio) return nullptr;
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    audio->uninitialize = SUCCEEDED(hr);
    audio->comThread = GetCurrentThreadId();
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) { delete audio; return nullptr; }
    hr = XAudio2Create(&audio->engine, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (SUCCEEDED(hr)) hr = audio->engine->CreateMasteringVoice(&audio->master);
    if (FAILED(hr)) { doom_audio_destroy(audio); return nullptr; }
    return audio;
}

extern "C" void doom_audio_stop_effects(DoomAudio *audio) {
    if (!audio) return;
    for (auto &slot : audio->voices) clear_voice(slot);
    doom_audio_weapon_stop(audio);
}

extern "C" void doom_audio_weapon_stop(DoomAudio *audio) {
    if (!audio) return;
    clear_voice(audio->weapon);
    audio->weaponClip = 0;
}

extern "C" int doom_audio_weapon_volume(DoomAudio *audio, float volume) {
    if (!audio || !audio->weapon.voice || !(volume >= 0 && volume <= 1)) return 0;
    return SUCCEEDED(audio->weapon.voice->SetVolume(volume));
}

static int weapon_pcm(DoomAudio *audio, unsigned clip_id, const int16_t *samples,
                      size_t count, unsigned rate, float volume, bool loop) {
    if (!audio || !clip_id || !samples || !count || rate < 1000 || rate > 192000 ||
        count > static_cast<size_t>(rate) * 60 || !(volume >= 0 && volume <= 1)) return 0;
    if (audio->weapon.voice && audio->weaponClip == clip_id)
        return doom_audio_weapon_volume(audio, volume);

    DoomVoice next{};
    next.samples = static_cast<int16_t *>(std::malloc(count * sizeof(int16_t)));
    if (!next.samples) return 0;
    std::memcpy(next.samples, samples, count * sizeof(int16_t));
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 1;
    format.nSamplesPerSec = rate; format.wBitsPerSample = 16;
    format.nBlockAlign = 2; format.nAvgBytesPerSec = rate * 2;
    HRESULT hr = audio->engine->CreateSourceVoice(&next.voice, &format);
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(count * sizeof(int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE *>(next.samples);
    if (loop) buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    if (SUCCEEDED(hr)) hr = next.voice->SetVolume(volume);
    if (SUCCEEDED(hr)) hr = next.voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) { clear_voice(next); return 0; }

    doom_audio_weapon_stop(audio);
    hr = next.voice->Start();
    if (FAILED(hr)) { clear_voice(next); return 0; }
    audio->weapon = next;
    audio->weaponClip = clip_id;
    return 1;
}

extern "C" int doom_audio_weapon_loop_pcm(DoomAudio *audio, unsigned clip_id,
                                          const int16_t *samples, size_t count,
                                          unsigned rate, float volume) {
    return weapon_pcm(audio, clip_id, samples, count, rate, volume, true);
}

extern "C" int doom_audio_weapon_once_pcm(DoomAudio *audio, unsigned clip_id,
                                          const int16_t *samples, size_t count,
                                          unsigned rate, float volume) {
    return weapon_pcm(audio, clip_id, samples, count, rate, volume, false);
}

extern "C" void doom_audio_destroy(DoomAudio *audio) {
    if (!audio) return;
    doom_audio_stop_effects(audio);
    doom_audio_music_stop(audio);
    if (audio->master) audio->master->DestroyVoice();
    if (audio->engine) audio->engine->Release();
    if (audio->uninitialize && audio->comThread == GetCurrentThreadId()) CoUninitialize();
    delete audio;
}

extern "C" void doom_audio_music_stop(DoomAudio *audio) {
    if (audio) clear_voice(audio->music);
}

extern "C" int doom_audio_music_volume(DoomAudio *audio, float volume) {
    if (!audio || !audio->music.voice || !(volume >= 0 && volume <= 1)) return 0;
    return SUCCEEDED(audio->music.voice->SetVolume(volume));
}

extern "C" int doom_audio_pause(DoomAudio *audio, int paused) {
    if (!audio || !audio->engine) return 0;
    bool want = paused != 0;
    if (want == audio->paused) return 1;
    if (want) audio->engine->StopEngine();
    else if (FAILED(audio->engine->StartEngine())) return 0;
    audio->paused = want;
    return 1;
}

extern "C" int doom_audio_music_adopt(DoomAudio *audio, int16_t *stereo,
                                      size_t frames, unsigned rate, float volume) {
    if (!audio || !stereo || !frames || rate < 8000 || rate > 48000 ||
        frames > static_cast<size_t>(rate) * 602 || !(volume >= 0 && volume <= 1)) return 0;
    DoomVoice next{};
    next.samples = stereo;
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2; fmt.nSamplesPerSec = rate;
    fmt.wBitsPerSample = 16; fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = rate * 4;
    HRESULT hr = audio->engine->CreateSourceVoice(&next.voice, &fmt);
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(frames * 4);
    buffer.pAudioData = reinterpret_cast<const BYTE *>(next.samples);
    buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    if (SUCCEEDED(hr)) hr = next.voice->SetVolume(volume);
    if (SUCCEEDED(hr)) hr = next.voice->SubmitSourceBuffer(&buffer);
    if (SUCCEEDED(hr)) hr = next.voice->Start();
    if (FAILED(hr)) { next.samples = nullptr; clear_voice(next); return 0; }
    clear_voice(audio->music);
    audio->music = next;
    return 1;
}

extern "C" int doom_audio_music_pcm(DoomAudio *audio, const int16_t *stereo,
                                    size_t frames, unsigned rate, float volume) {
    if (!audio || !stereo || !frames || rate < 8000 || rate > 48000 ||
        frames > static_cast<size_t>(rate) * 602 || !(volume >= 0 && volume <= 1)) return 0;
    auto *copy = static_cast<int16_t *>(std::malloc(frames * 2 * sizeof(int16_t)));
    if (!copy) return 0;
    std::memcpy(copy, stereo, frames * 2 * sizeof(int16_t));
    if (doom_audio_music_adopt(audio, copy, frames, rate, volume)) return 1;
    std::free(copy);
    return 0;
}

extern "C" void doom_audio_poll(DoomAudio *audio) {
    if (!audio) return;
    if (audio->weapon.voice) {
        XAUDIO2_VOICE_STATE state{};
        audio->weapon.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (!state.BuffersQueued) doom_audio_weapon_stop(audio);
    }
    for (auto &slot : audio->voices) if (slot.voice) {
        XAUDIO2_VOICE_STATE state{};
        slot.voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (!state.BuffersQueued) clear_voice(slot);
    }
}

extern "C" int doom_audio_play_pcm(DoomAudio *audio, const int16_t *samples,
                                    size_t count, unsigned rate, float volume) {
    if (!audio || !samples || !count || rate < 1000 || rate > 192000 ||
        count > static_cast<size_t>(rate) * 60 || !(volume >= 0 && volume <= 1)) return 0;
    doom_audio_poll(audio);
    DoomVoice *slot = nullptr;
    for (auto &candidate : audio->voices) if (!candidate.voice) { slot = &candidate; break; }
    if (!slot) return 0;
    slot->samples = static_cast<int16_t *>(std::malloc(count * sizeof(int16_t)));
    if (!slot->samples) return 0;
    std::memcpy(slot->samples, samples, count * sizeof(int16_t));
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM; format.nChannels = 1;
    format.nSamplesPerSec = rate; format.wBitsPerSample = 16;
    format.nBlockAlign = 2; format.nAvgBytesPerSec = rate * 2;
    HRESULT hr = audio->engine->CreateSourceVoice(&slot->voice, &format);
    XAUDIO2_BUFFER buffer{};
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    buffer.AudioBytes = static_cast<UINT32>(count * sizeof(int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE *>(slot->samples);
    if (SUCCEEDED(hr)) hr = slot->voice->SetVolume(volume);
    if (SUCCEEDED(hr)) hr = slot->voice->SubmitSourceBuffer(&buffer);
    if (SUCCEEDED(hr)) hr = slot->voice->Start();
    if (FAILED(hr)) { clear_voice(*slot); return 0; }
    return 1;
}
