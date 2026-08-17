/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <hal/hal.h>

/**
 * @brief Records microphone audio straight to a WAV file on the SD card
 *        (length limited only by free space). Opens to a Record/History
 *        menu; History lists past recordings for playback or deletion.
 *
 */
class AppRecord : public mooncake::AppAbility {
public:
    AppRecord();
    ~AppRecord();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class State { Idle, MainMenu, Recording, History };

    static constexpr const char* RECORD_DIR      = "/sdcard/records";
    static constexpr size_t CHUNK_LENGTH          = 200;  // samples per read, matches screen width
    static constexpr size_t RECORD_SAMPLERATE     = 16000;
    static constexpr size_t PLAYBACK_SAMPLERATE   = 16000;
    static constexpr int PLAYBACK_CHANNEL         = 0;
    static constexpr size_t HISTORY_VISIBLE_ROWS  = 6;
    // Recordings are quiet even with reasonable mic gain, so play them back
    // louder than audio::DEFAULT_VOLUME (90, tuned for UI sound effects).
    static constexpr uint8_t PLAYBACK_VOLUME      = 220;

    State _state              = State::Idle;
    FILE* _rec_file            = nullptr;
    std::string _rec_path;
    uint32_t _rec_data_bytes  = 0;
    int16_t _chunk_buf[CHUNK_LENGTH] = {};

    // Main menu
    int _menu_index = 0;  // 0 = Record, 1 = History

    // History
    std::vector<std::string> _history_files;  // file names only, under RECORD_DIR
    int _history_index          = 0;
    bool _history_delete_confirm = false;

    bool open_new_recording_file();
    void finalize_recording_file();
    void abort_recording_file();

    void start_recording();
    void stop_recording_and_play();
    void play_file(const std::string& path);

    void enter_main_menu();
    void handle_menu_key(const Keyboard::KeyEvent_t& keyEvent);
    void render_page_menu();

    void handle_recording_key(const Keyboard::KeyEvent_t& keyEvent);

    void enter_history();
    void scan_history_files();
    void handle_history_key(const Keyboard::KeyEvent_t& keyEvent);
    void render_page_history();

    void render_page_recording();
    void render_page_playing();
    void render_page_error(const std::string& msg);
    void render_waveform();
};
