/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <hal/hal.h>
#include <cstdint>
#include <string>
#include <vector>

class AppTimeMachine : public mooncake::AppAbility {
public:
    AppTimeMachine();
    ~AppTimeMachine();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

    struct Anchor {
        float year;
        float price;
        std::string label;
    };

    struct EventMarker {
        float year;
        std::string label;
        std::string mood;
    };

    struct SeriesPoint {
        float year;
        float price;
    };

    struct StoryData {
        std::string title;
        std::string symbol;
        float initialInvestment = 1000.0f;
        float startYear = 2010.0f;
        float endYear = 2026.62f;
        float startPrice = 1.13f;
        std::vector<Anchor> anchors;
        std::vector<EventMarker> events;
        std::string path;
        bool loadedFromSd = false;
    };

    struct StoryEntry {
        std::string title;
        std::string symbol;
        std::string path;
    };

private:
    struct PlaybackState {
        float currentYear  = 2010.0f;
        float currentPrice = 1.13f;
        float speed        = 1.0f;
        bool started       = false;
        bool playing       = false;
        bool ending        = false;
        bool finished      = false;
    };

    struct ChartCamera {
        float centerYear   = 2010.0f;
        float centerPrice  = 1.13f;
        float yearWindow   = 5.0f;
        float priceWindow  = 2.0f;
    };

    PlaybackState _playback;
    ChartCamera _camera;
    StoryData _story;
    std::vector<StoryEntry> _stories;
    std::vector<SeriesPoint> _series;
    int _selected_story_index = 0;
    int _key_event_slot_id = -1;
    uint32_t _last_update_ms = 0;
    uint32_t _last_render_ms = 0;
    float _peak_value = 1000.0f;
    float _summary_transition = 0.0f;
    ChartCamera _exit_camera;

    void scan_story_library();
    void seed_story_library();
    bool load_story_from_path(const std::string& path, StoryData& story) const;
    bool open_selected_story(bool startPlaying);
    bool parse_story_json(const char* json, StoryData& story) const;
    bool validate_story(StoryData& story) const;
    void build_series();
    void reset_playback(bool startPlaying);
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void update_playback(float dt);
    void update_camera(float dt);
    void render();
    void render_library();
    void render_empty_library();
    void render_story();
    void render_transition();
    void render_summary();
    void draw_live_chart(int x, int y, int w, int h, const char* mood, bool showMoodFace);
    void draw_transition_chart(int x, int y, int w, int h, float progress);
    void draw_summary_chart(int x, int y, int w, int h);
    void draw_event_markers(int x, int y, int w, int h, float leftYear, float rightYear, float minPrice, float maxPrice);
    void draw_status_text(int width, int height, float value, float pct);
    void draw_icon(int cx, int cy, int size, uint16_t color);
    void draw_mood_face(int cx, int cy, const char* mood, uint16_t color);
    float price_at_year(float year) const;
    const char* mood_for(float pct, float drawdown, float velocity) const;
    uint16_t mood_color(const char* mood) const;
    float map_year_to_x(float year, int x, int w, float leftYear, float rightYear) const;
    float map_price_to_y(float price, int y, int h, float minPrice, float maxPrice) const;
};
