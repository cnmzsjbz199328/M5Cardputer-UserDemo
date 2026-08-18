/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include <array>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <algorithm>
#include <esp_heap_caps.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <random>
#include "audio.h"

namespace audio {

static bool quiet_mode_is_enabled = false;

namespace {

// M5Unified's speaker task keeps the raw-data pointer and consumes it
// asynchronously; it does not copy the samples. Keep several buffers alive
// and rotate them so a short sound is not overwritten while it is playing.
constexpr std::size_t kAudioBufferCount = 3;
std::array<std::vector<int16_t>, kAudioBufferCount> tone_buffers;
std::array<std::vector<int16_t>, kAudioBufferCount> melody_buffers;
std::size_t tone_buffer_index = 0;
std::size_t melody_buffer_index = 0;

// This device's heap is small (a few hundred KB total) and WiFi/TLS use can
// leave it deeply fragmented; a resize() here can genuinely fail to find a
// large enough block. The project builds with -fno-exceptions, so a failed
// allocation calls abort() directly rather than throwing — there is nothing
// to catch. Check the largest free block up front instead and skip the
// sound (audio is decorative, not essential) rather than crash the app.
//
// libstdc++'s vector growth policy does not just top up to `needed` when
// growing: _M_check_len() picks size()+max(size(), extra), which can round
// up to roughly double the buffer's PREVIOUS content size. That made an
// earlier version of this check compare the heap against the wrong (much
// smaller) number while the real resize() asked for far more and still
// aborted. Force an exact allocation instead: drop the old capacity first
// (free) so the resize below has no prior size to double against.
std::vector<int16_t>* acquire_buffer(std::vector<int16_t>& buffer, std::size_t needed, const char* what)
{
    if (needed > buffer.capacity()) {
        std::size_t needed_bytes = needed * sizeof(int16_t);
        if (heap_caps_get_largest_free_block(MALLOC_CAP_8BIT) <= needed_bytes + 4096) {
            mclog::tagWarn("audio", "{} buffer allocation would fail, skipping sound", what);
            return nullptr;
        }
        buffer.clear();
        buffer.shrink_to_fit();
    }
    buffer.resize(needed);
    return &buffer;
}

std::vector<int16_t>* acquire_tone_buffer(std::size_t sample_count)
{
    auto& buffer = tone_buffers[tone_buffer_index];
    tone_buffer_index = (tone_buffer_index + 1) % kAudioBufferCount;
    return acquire_buffer(buffer, sample_count * 2, "tone");
}

std::vector<int16_t>* acquire_melody_buffer(std::size_t sample_count)
{
    auto& buffer = melody_buffers[melody_buffer_index];
    melody_buffer_index = (melody_buffer_index + 1) % kAudioBufferCount;
    return acquire_buffer(buffer, sample_count * 2, "melody");
}

}  // namespace

static std::vector<int> c_major_scale = {60, 62, 64, 65, 67, 69, 71};  // C大调音阶（C D E F G A B）

void play_tone(int frequency, double durationSec)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    const int sample_rate = GetHAL().speaker.config().sample_rate;
    const int samples     = static_cast<int>(sample_rate * durationSec);

    const int fade_len    = 200;  // 淡出长度（采样点）
    const float amplitude = 32767.0f / 5;

    // Write directly into the persistent (rotating) buffer instead of
    // building a temporary vector and copying it in: with two full-size
    // buffers briefly alive at once, that copy could fail to allocate under
    // heap fragmentation (e.g. right after a WiFi/TLS session) and abort().
    auto* buffer = acquire_tone_buffer(samples);
    if (!buffer) {
        return;
    }
    for (int i = 0; i < samples; ++i) {
        float amp = amplitude;

        // 应用结尾淡出（fade-out）
        if (i >= samples - fade_len) {
            float fade_factor = static_cast<float>(samples - i) / fade_len;
            amp *= fade_factor;
        }

        int16_t value        = static_cast<int16_t>(amp * sin(2.0 * M_PI * frequency * i / sample_rate));
        (*buffer)[i * 2]     = value;  // 左声道
        (*buffer)[i * 2 + 1] = value;  // 右声道
    }

    GetHAL().speaker.playRaw(buffer->data(), buffer->size());
}

void play_melody(const std::vector<int>& midiList, double durationSec)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    const int sample_rate      = GetHAL().speaker.config().sample_rate;
    const int samples_per_note = static_cast<int>(sample_rate * durationSec);
    const int fade_len         = 200;  // 每个音符结尾的淡出长度
    const float amplitude      = 32767.0f / 5;

    // Write directly into the persistent (rotating) buffer instead of
    // building a temporary vector and copying it in; see play_tone().
    auto* buffer = acquire_melody_buffer(midiList.size() * samples_per_note);
    if (!buffer) {
        return;
    }
    std::size_t write_index = 0;

    for (int midiNote : midiList) {
        for (int i = 0; i < samples_per_note; ++i) {
            float amp = amplitude;

            // 应用淡出（仅每个音符的结尾）
            if (i >= samples_per_note - fade_len) {
                float fade_factor = static_cast<float>(samples_per_note - i) / fade_len;
                amp *= fade_factor;
            }

            int16_t sample = 0;
            if (midiNote >= 0) {
                double freq = 440.0 * pow(2.0, (midiNote - 69) / 12.0);
                sample      = static_cast<int16_t>(amp * sin(2.0 * M_PI * freq * i / sample_rate));
            }

            (*buffer)[write_index++] = sample;  // 左声道
            (*buffer)[write_index++] = sample;  // 右声道
        }
    }

    GetHAL().speaker.playRaw(buffer->data(), buffer->size());
}

void play_tone_from_midi(int midi, double durationSec)
{
    if (GetHAL().speaker.getVolume() <= 0) {
        return;
    }

    double freq = 440.0 * std::pow(2.0, (midi - 69) / 12.0);
    play_tone(static_cast<int>(freq), durationSec);
}

void play_random_tone(int semitoneShift, double durationSec)
{
    // No random sounds during quiet mode
    if (GetHAL().speaker.getVolume() <= 0 || is_quiet_mode()) {
        return;
    }

    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, static_cast<int>(c_major_scale.size()) - 1);

    int index = dist(gen);
    int midi  = c_major_scale[index] + semitoneShift;

    play_tone_from_midi(midi, durationSec);
}

/* -------------------------------------------------------------------------- */
/*                                  Keyboard                                  */
/* -------------------------------------------------------------------------- */
static void _keyboard_sfx_on_key_event(const Keyboard::KeyEvent_t& event)
{
    // No keypress sounds during quiet mode
    if (!event.state || is_quiet_mode()) {
        return;
    }

    GetHAL().speaker.setVolume(GetHAL().getVolume());

    int semitoneShift = 48;
    switch (event.keyCode) {
        case KEY_1:
            play_tone_from_midi(c_major_scale[0] + semitoneShift, 0.02);
            return;
        case KEY_2:
            play_tone_from_midi(c_major_scale[1] + semitoneShift, 0.02);
            return;
        case KEY_3:
            play_tone_from_midi(c_major_scale[2] + semitoneShift, 0.02);
            return;
        case KEY_4:
            play_tone_from_midi(c_major_scale[3] + semitoneShift, 0.02);
            return;
        case KEY_5:
            play_tone_from_midi(c_major_scale[4] + semitoneShift, 0.02);
            return;
        case KEY_6:
            play_tone_from_midi(c_major_scale[5] + semitoneShift, 0.02);
            return;
        case KEY_7:
            play_tone_from_midi(c_major_scale[6] + semitoneShift, 0.02);
            return;
        default:
            play_random_tone(semitoneShift, 0.02);
    }
}

void set_keyboard_sfx_enable(bool enable)
{
    static size_t slot_id  = 0;
    static bool is_enabled = false;

    mclog::tagInfo("audio", "set keyboard sfx enable: {}", enable);
    if (enable) {
        if (is_enabled) {
            return;
        }
        is_enabled = true;
        slot_id    = GetHAL().keyboard.onKeyEvent.connect(_keyboard_sfx_on_key_event);
    } else {
        if (!is_enabled) {
            return;
        }
        is_enabled = false;
        GetHAL().keyboard.onKeyEvent.disconnect(slot_id);
    }
}

bool is_quiet_mode()
{
    return quiet_mode_is_enabled;
}

void set_quiet_mode(bool quiet)
{
    quiet_mode_is_enabled = quiet;
}

}  // namespace audio
