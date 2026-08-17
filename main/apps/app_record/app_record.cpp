/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_record.h"
#include "assets/record_big.h"
#include "assets/record_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/audio/mp3_decoder.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <sys/stat.h>
#include <dirent.h>
#include <algorithm>
#include <cerrno>
#include <cstring>

using namespace mooncake;

namespace {

#pragma pack(push, 1)
struct WavHeader {
    char riff[4]           = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size    = 0;
    char wave[4]           = {'W', 'A', 'V', 'E'};
    char fmt[4]            = {'f', 'm', 't', ' '};
    uint32_t fmt_size      = 16;
    uint16_t audio_format  = 1;  // PCM
    uint16_t num_channels  = 1;
    uint32_t sample_rate   = 0;
    uint32_t byte_rate     = 0;
    uint16_t block_align   = 0;
    uint16_t bits_per_sample = 16;
    char data[4]           = {'d', 'a', 't', 'a'};
    uint32_t data_size     = 0;
};
#pragma pack(pop)

}  // namespace

AppRecord::AppRecord()
{
    setAppInfo().name     = "Record";
    setAppInfo().userData = new AppIcon_t(image_data_record_big, image_data_record_small);
}

AppRecord::~AppRecord()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppRecord::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    audio::set_keyboard_sfx_enable(false);

    if (!GetHAL().sdCardProbe().is_mounted) {
        mclog::tagWarn(getAppInfo().name, "sd card not mounted");
        render_page_error("No SD card,\ncan't record");
        _state = State::Idle;
        return;
    }

    enter_main_menu();
}

void AppRecord::onRunning()
{
    if (_state == State::Recording && GetHAL().mic.isEnabled()) {
        render_waveform();
    }

    // Handle keyboard input
    auto key_event = GetHAL().keyboard.getLatestKeyEvent();
    if (key_event.state && !key_event.isModifier) {
        switch (_state) {
            case State::MainMenu:
                handle_menu_key(key_event);
                break;
            case State::Recording:
                handle_recording_key(key_event);
                break;
            case State::History:
                handle_history_key(key_event);
                break;
            default:
                break;
        }
    }

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        close();
    }
}

void AppRecord::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    // Stop recording if active
    while (GetHAL().mic.isRecording()) {
        GetHAL().delay(1);
    }
    GetHAL().mic.end();

    finalize_recording_file();

    // Cleanup audio devices
    GetHAL().speaker.begin();
    GetHAL().speaker.setVolume(audio::DEFAULT_VOLUME);

    audio::set_keyboard_sfx_enable(true);
}

bool AppRecord::open_new_recording_file()
{
    struct stat st;
    if (stat(RECORD_DIR, &st) != 0) {
        if (mkdir(RECORD_DIR, 0775) != 0 && errno != EEXIST) {
            mclog::tagError(getAppInfo().name, "mkdir {} failed: {}", RECORD_DIR, std::strerror(errno));
            return false;
        }
    }

    // FATFS is built with CONFIG_FATFS_LFN_NONE, so filenames must fit 8.3
    // (8-char name + 3-char extension) or fopen() fails with FR_INVALID_NAME.
    _rec_file = nullptr;
    for (uint32_t i = 1; i <= 99999; ++i) {
        auto name = fmt::format("REC{:05d}.WAV", i);
        auto path = std::string(RECORD_DIR) + "/" + name;

        struct stat fst;
        if (stat(path.c_str(), &fst) == 0) {
            continue;  // already taken
        }

        _rec_path = path;
        _rec_file = std::fopen(_rec_path.c_str(), "wb");
        break;
    }

    if (!_rec_file) {
        mclog::tagError(getAppInfo().name, "open {} failed: {}", _rec_path, std::strerror(errno));
        return false;
    }

    // Placeholder header, patched with real sizes once recording stops
    WavHeader header;
    header.sample_rate = RECORD_SAMPLERATE;
    header.byte_rate   = RECORD_SAMPLERATE * sizeof(int16_t);
    header.block_align = sizeof(int16_t);
    std::fwrite(&header, sizeof(header), 1, _rec_file);

    _rec_data_bytes = 0;
    mclog::tagInfo(getAppInfo().name, "recording to {}", _rec_path);
    return true;
}

void AppRecord::finalize_recording_file()
{
    if (!_rec_file) {
        return;
    }

    WavHeader header;
    header.sample_rate = RECORD_SAMPLERATE;
    header.byte_rate   = RECORD_SAMPLERATE * sizeof(int16_t);
    header.block_align = sizeof(int16_t);
    header.data_size   = _rec_data_bytes;
    header.chunk_size  = 36 + _rec_data_bytes;

    std::fseek(_rec_file, 0, SEEK_SET);
    std::fwrite(&header, sizeof(header), 1, _rec_file);
    std::fclose(_rec_file);
    _rec_file = nullptr;

    mclog::tagInfo(getAppInfo().name, "saved {} ({} bytes)", _rec_path, _rec_data_bytes);
}

void AppRecord::abort_recording_file()
{
    if (!_rec_file) {
        return;
    }

    std::fclose(_rec_file);
    _rec_file = nullptr;
    std::remove(_rec_path.c_str());
    mclog::tagInfo(getAppInfo().name, "discarded {}", _rec_path);
}

void AppRecord::start_recording()
{
    // Since microphone and speaker cannot be used at the same time, turn off speaker
    GetHAL().speaker.end();

    auto cfg                = GetHAL().mic.config();
    // Cardputer's onboard mic default gain is 16; a much larger value here
    // amplifies the noise floor along with the voice signal, so keep this
    // modest. noise_filter_level smooths sample-to-sample noise a bit.
    cfg.magnification      = 32;
    cfg.noise_filter_level = 3;
    GetHAL().mic.config(cfg);
    GetHAL().mic.begin();

    if (!open_new_recording_file()) {
        render_page_error("SD write\nfailed");
        _state = State::Idle;
        return;
    }

    _state = State::Recording;
    render_page_recording();
}

void AppRecord::stop_recording_and_play()
{
    // Stop recording and finalize the file
    while (GetHAL().mic.isRecording()) {
        GetHAL().delay(1);
    }
    GetHAL().mic.end();
    finalize_recording_file();

    if (GetHAL().speaker.isEnabled()) {
        GetHAL().speaker.begin();
        GetHAL().speaker.setVolume(PLAYBACK_VOLUME);

        render_page_playing();
        play_file(_rec_path);
    }

    enter_main_menu();
}

void AppRecord::play_file(const std::string& path)
{
    if (audio::is_mp3_file(path)) {
        audio::play_mp3_file(path, PLAYBACK_CHANNEL);
        return;
    }

    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        mclog::tagError(getAppInfo().name, "playback open {} failed: {}", path, std::strerror(errno));
        return;
    }
    std::fseek(f, sizeof(WavHeader), SEEK_SET);

    // Two alternating buffers streamed straight from SD, so playback length
    // isn't limited by RAM.
    static constexpr size_t PLAY_CHUNK_SAMPLES = 1024;
    static int16_t buf[2][PLAY_CHUNK_SAMPLES];
    int buf_idx  = 0;
    bool is_first = true;

    size_t samples_read;
    while ((samples_read = std::fread(buf[buf_idx], sizeof(int16_t), PLAY_CHUNK_SAMPLES, f)) > 0) {
        // Wait for a free double-buffer slot on this channel before handing
        // over the next chunk (M5Unified keeps 2 pending buffers/channel).
        while (GetHAL().speaker.isPlaying(PLAYBACK_CHANNEL) >= 2) {
            GetHAL().delay(1);
        }
        GetHAL().speaker.playRaw(buf[buf_idx], samples_read, PLAYBACK_SAMPLERATE, false, 1, PLAYBACK_CHANNEL,
                                  is_first);
        is_first = false;
        buf_idx ^= 1;
    }
    std::fclose(f);

    while (GetHAL().speaker.isPlaying(PLAYBACK_CHANNEL)) {
        GetHAL().delay(1);
    }
}

void AppRecord::enter_main_menu()
{
    _state = State::MainMenu;
    render_page_menu();
}

void AppRecord::handle_menu_key(const Keyboard::KeyEvent_t& keyEvent)
{
    if (keyEvent.keyCode == KEY_UP || keyEvent.keyCode == KEY_COMMA || keyEvent.keyCode == KEY_DOWN ||
        keyEvent.keyCode == KEY_DOT) {
        _menu_index = _menu_index == 0 ? 1 : 0;
        render_page_menu();
    } else if (keyEvent.keyCode == KEY_ENTER) {
        if (_menu_index == 0) {
            start_recording();
        } else {
            enter_history();
        }
    }
}

void AppRecord::handle_recording_key(const Keyboard::KeyEvent_t& keyEvent)
{
    if (keyEvent.keyCode == KEY_ENTER) {
        stop_recording_and_play();
    } else if (keyEvent.keyCode == KEY_ESC || keyEvent.keyCode == KEY_GRAVE) {
        while (GetHAL().mic.isRecording()) {
            GetHAL().delay(1);
        }
        GetHAL().mic.end();
        abort_recording_file();
        enter_main_menu();
    }
}

void AppRecord::enter_history()
{
    scan_history_files();
    _history_index          = 0;
    _history_delete_confirm = false;
    _state                  = State::History;
    render_page_history();
}

void AppRecord::scan_history_files()
{
    _history_files.clear();

    DIR* dir = opendir(RECORD_DIR);
    if (!dir) {
        return;
    }

    while (struct dirent* entry = readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        _history_files.emplace_back(entry->d_name);
    }
    closedir(dir);

    std::sort(_history_files.begin(), _history_files.end());
}

void AppRecord::handle_history_key(const Keyboard::KeyEvent_t& keyEvent)
{
    if (_history_delete_confirm) {
        if (keyEvent.keyCode == KEY_ENTER) {
            auto path = std::string(RECORD_DIR) + "/" + _history_files[_history_index];
            std::remove(path.c_str());
            mclog::tagInfo(getAppInfo().name, "deleted {}", path);
            _history_delete_confirm = false;
            scan_history_files();
            if (_history_index >= static_cast<int>(_history_files.size())) {
                _history_index = static_cast<int>(_history_files.size()) - 1;
            }
        } else if (keyEvent.keyCode == KEY_ESC || keyEvent.keyCode == KEY_GRAVE) {
            _history_delete_confirm = false;
        } else {
            return;
        }
        render_page_history();
        return;
    }

    if (keyEvent.keyCode == KEY_ESC || keyEvent.keyCode == KEY_GRAVE) {
        enter_main_menu();
        return;
    }

    if (_history_files.empty()) {
        return;
    }

    if (keyEvent.keyCode == KEY_UP || keyEvent.keyCode == KEY_COMMA) {
        _history_index = (_history_index + static_cast<int>(_history_files.size()) - 1) %
                         static_cast<int>(_history_files.size());
        render_page_history();
    } else if (keyEvent.keyCode == KEY_DOWN || keyEvent.keyCode == KEY_DOT) {
        _history_index = (_history_index + 1) % static_cast<int>(_history_files.size());
        render_page_history();
    } else if (keyEvent.keyCode == KEY_ENTER) {
        auto path = std::string(RECORD_DIR) + "/" + _history_files[_history_index];
        GetHAL().speaker.begin();
        GetHAL().speaker.setVolume(PLAYBACK_VOLUME);
        render_page_playing();
        play_file(path);
        render_page_history();
    } else if (keyEvent.keyCode == KEY_DELETE || keyEvent.keyCode == KEY_BACKSPACE) {
        _history_delete_confirm = true;
        render_page_history();
    }
}

void AppRecord::render_page_menu()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);

    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, 0);
    GetHAL().canvas.print("Record");

    const char* options[2] = {"Record", "History"};
    for (int i = 0; i < 2; ++i) {
        GetHAL().canvas.setCursor(10, 24 + i * 16);
        GetHAL().canvas.setTextColor(i == _menu_index ? TFT_GREEN : TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.printf("%c %s", i == _menu_index ? '>' : ' ', options[i]);
    }

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, GetHAL().canvas.height() - 12);
    GetHAL().canvas.print("Up/Down select  Enter open");

    GetHAL().pushCanvas();
}

void AppRecord::render_page_recording()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, 0);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.print("Enter: save & play   Esc: discard");
    GetHAL().pushCanvas();
}

void AppRecord::render_page_playing()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, 0);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.print("playing");
    GetHAL().pushCanvas();
}

void AppRecord::render_page_error(const std::string& msg)
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, 0);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.print(msg.c_str());
    GetHAL().pushCanvas();
}

void AppRecord::render_page_history()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);

    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, 0);
    GetHAL().canvas.printf("History (%u)", static_cast<unsigned>(_history_files.size()));

    if (_history_delete_confirm) {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(10, 20);
        GetHAL().canvas.printf("Delete %s ?", _history_files[_history_index].c_str());
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(10, GetHAL().canvas.height() - 12);
        GetHAL().canvas.print("Enter confirm  Esc cancel");
        GetHAL().pushCanvas();
        return;
    }

    if (_history_files.empty()) {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(10, 24);
        GetHAL().canvas.print("No recordings yet");
    } else {
        const size_t count = _history_files.size();
        size_t first        = 0;
        if (_history_index >= static_cast<int>(HISTORY_VISIBLE_ROWS / 2)) {
            first = _history_index - HISTORY_VISIBLE_ROWS / 2;
        }
        if (count > HISTORY_VISIBLE_ROWS && first + HISTORY_VISIBLE_ROWS > count) {
            first = count - HISTORY_VISIBLE_ROWS;
        }

        const size_t visible_count = (count - first < HISTORY_VISIBLE_ROWS) ? count - first : HISTORY_VISIBLE_ROWS;
        for (size_t row = 0; row < visible_count; ++row) {
            const size_t idx = first + row;
            GetHAL().canvas.setCursor(10, 20 + static_cast<int>(row) * 14);
            GetHAL().canvas.setTextColor(static_cast<int>(idx) == _history_index ? TFT_GREEN : TFT_WHITE,
                                          THEME_COLOR_BG);
            GetHAL().canvas.printf("%c %s", static_cast<int>(idx) == _history_index ? '>' : ' ',
                                    _history_files[idx].c_str());
        }
    }

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, GetHAL().canvas.height() - 12);
    GetHAL().canvas.print("Enter play  Del delete  Esc back");

    GetHAL().pushCanvas();
}

void AppRecord::render_waveform()
{
    if (GetHAL().mic.record(_chunk_buf, CHUNK_LENGTH, RECORD_SAMPLERATE)) {
        if (_rec_file) {
            size_t written = std::fwrite(_chunk_buf, sizeof(int16_t), CHUNK_LENGTH, _rec_file);
            _rec_data_bytes += written * sizeof(int16_t);
        }

        // 清除波形区域（避免重叠绘制）
        int32_t waveform_top    = 15;  // 文字下方
        int32_t waveform_height = GetHAL().canvas.height() - waveform_top;
        GetHAL().canvas.fillRect(10, waveform_top, CHUNK_LENGTH, waveform_height, THEME_COLOR_BG);

        int32_t w = GetHAL().canvas.width();
        if (w > (int32_t)CHUNK_LENGTH) {
            w = CHUNK_LENGTH;
        }

        // 直接用录音数据画点 - 无需额外缓冲区
        int32_t center_y           = waveform_top + waveform_height / 2;
        static constexpr int shift = 8;  // 调整振幅显示

        for (int32_t x = 0; x < w; ++x) {
            // 计算波形点的y坐标
            int32_t sample_value = _chunk_buf[x] >> shift;
            int32_t y            = center_y + sample_value;

            // 限制在波形区域内
            if (y < waveform_top) y = waveform_top;
            if (y >= waveform_top + waveform_height) y = waveform_top + waveform_height - 1;

            // 画点 - 使用2x2像素块让点更明显
            GetHAL().canvas.drawPixel(x + 10, y, TFT_WHITE);
            GetHAL().canvas.drawPixel(x + 11, y, TFT_WHITE);
            GetHAL().canvas.drawPixel(x + 10, y + 1, TFT_WHITE);
            GetHAL().canvas.drawPixel(x + 11, y + 1, TFT_WHITE);
        }

        // 重绘UI文字
        GetHAL().canvas.setCursor(10, 0);
        GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
        GetHAL().canvas.setTextSize(1);
        GetHAL().canvas.print("Enter: save & play   Esc: discard");

        GetHAL().pushCanvas();
    }
}
