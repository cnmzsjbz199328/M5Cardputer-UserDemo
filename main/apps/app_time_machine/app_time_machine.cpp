/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_time_machine.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <assets.h>
#include <cJSON.h>
#include <hal.h>
#include <mooncake_log.h>
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <string>
#include <sys/stat.h>

using namespace mooncake;

namespace {

constexpr uint32_t kRenderIntervalMs = 33;
constexpr const char* kStoryRoots[] = {
    // FATFS may expose the long directory name as its 8.3 short name when
    // long-file-name support is unavailable.  Windows shows this directory
    // as "time_machine", while the Cardputer currently enumerates TIME_M~1.
    "/sdcard/APPS/TIME_M~1",
    "/sdcard/APPS/time_machine",
    "/sdcard/apps/time_machine",
};
constexpr const char* kStoryFileName = "story.json";
uint16_t image_data_time_machine_big[56 * 56];
uint16_t image_data_time_machine_small[40 * 40];

bool is_story_file_name(const char* name)
{
    if (!name || strncasecmp(name, "story", 5) != 0) return false;
    const char* dot = std::strrchr(name, '.');
    return dot && (strcasecmp(dot, ".json") == 0 || strcasecmp(dot, ".jso") == 0);
}

float clampf(float v, float lo, float hi)
{
    return std::max(lo, std::min(hi, v));
}

float lerpf(float a, float b, float t)
{
    return a + (b - a) * clampf(t, 0.0f, 1.0f);
}

float smooth_toward(float current, float target, float stiffness, float dt)
{
    return lerpf(current, target, 1.0f - std::exp(-stiffness * dt));
}

uint32_t mulberry32(uint32_t& seed)
{
    seed += 0x6D2B79F5;
    uint32_t t = seed;
    t = (t ^ (t >> 15)) * (t | 1);
    t ^= t + ((t ^ (t >> 7)) * (t | 61));
    return t ^ (t >> 14);
}

float rand01(uint32_t& seed)
{
    return static_cast<float>(mulberry32(seed)) / 4294967295.0f;
}

void put_pixel(uint16_t* buffer, int width, int height, int x, int y, uint16_t color)
{
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    buffer[y * width + x] = color;
}

void draw_line_to_buffer(uint16_t* buffer, int width, int height, int x0, int y0, int x1, int y1, uint16_t color)
{
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        put_pixel(buffer, width, height, x0, y0, color);
        put_pixel(buffer, width, height, x0 + 1, y0, color);
        put_pixel(buffer, width, height, x0, y0 + 1, color);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_circle_to_buffer(uint16_t* buffer, int width, int height, int cx, int cy, int r, uint16_t color)
{
    int x = -r;
    int y = 0;
    int err = 2 - 2 * r;
    do {
        put_pixel(buffer, width, height, cx - x, cy + y, color);
        put_pixel(buffer, width, height, cx - y, cy - x, color);
        put_pixel(buffer, width, height, cx + x, cy - y, color);
        put_pixel(buffer, width, height, cx + y, cy + x, color);
        const int e = err;
        if (e <= y) err += ++y * 2 + 1;
        if (e > x || err > y) err += ++x * 2 + 1;
    } while (x < 0);
}

void build_time_machine_icon(uint16_t* buffer, int width, int height)
{
    constexpr uint16_t bg = 0x0841;
    constexpr uint16_t cyan = 0x07ff;
    constexpr uint16_t green = 0x07e0;
    constexpr uint16_t white = 0xffff;
    constexpr uint16_t red = 0xf800;

    std::fill(buffer, buffer + width * height, bg);

    const int cx = width / 2;
    const int cy = height / 2 - 1;
    const int r = std::min(width, height) / 2 - 7;
    draw_circle_to_buffer(buffer, width, height, cx, cy, r, cyan);
    draw_circle_to_buffer(buffer, width, height, cx, cy, r - 1, cyan);
    draw_line_to_buffer(buffer, width, height, cx, cy, cx, cy - r + 6, white);
    draw_line_to_buffer(buffer, width, height, cx, cy, cx + r - 7, cy + 4, white);

    draw_line_to_buffer(buffer, width, height, cx - r + 6, cy + r - 5, cx - r / 3, cy + 5, green);
    draw_line_to_buffer(buffer, width, height, cx - r / 3, cy + 5, cx + 2, cy + r - 7, green);
    draw_line_to_buffer(buffer, width, height, cx + 2, cy + r - 7, cx + r - 4, cy - r / 3, green);
    put_pixel(buffer, width, height, cx + r - 4, cy - r / 3, red);
    put_pixel(buffer, width, height, cx + r - 3, cy - r / 3, red);
    put_pixel(buffer, width, height, cx + r - 4, cy - r / 3 + 1, red);
}

void ensure_launcher_icons()
{
    static bool built = false;
    if (built) return;
    build_time_machine_icon(image_data_time_machine_big, 56, 56);
    build_time_machine_icon(image_data_time_machine_small, 40, 40);
    built = true;
}

}  // namespace

AppTimeMachine::AppTimeMachine()
{
    ensure_launcher_icons();
    setAppInfo().name = "Stocks";
    setAppInfo().userData = new AppIcon_t(image_data_time_machine_big, image_data_time_machine_small);
}

AppTimeMachine::~AppTimeMachine()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppTimeMachine::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // SD-card story text is length-capped, but keep wrap off defensively so
    // any long string clips instead of wrapping onto other UI elements.
    GetHAL().canvas.setTextWrap(false);

    scan_story_library();
    if (!_stories.empty()) {
        open_selected_story(false);
    }
    reset_playback(false);
    _last_update_ms = GetHAL().millis();
    _last_render_ms = 0;

    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    render();
}

void AppTimeMachine::onRunning()
{
    const uint32_t now = GetHAL().millis();
    const float dt = clampf((now - _last_update_ms) / 1000.0f, 0.0f, 0.12f);
    _last_update_ms = now;

    update_playback(dt);
    update_camera(dt);

    if (now - _last_render_ms >= kRenderIntervalMs) {
        render();
        _last_render_ms = now;
    }

    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppTimeMachine::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    GetHAL().canvas.setTextWrap(true);
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }
}

void AppTimeMachine::scan_story_library()
{
    _stories.clear();
    _series.clear();
    _story = StoryData{};
    _selected_story_index = 0;

    const auto probe = GetHAL().sdCardProbe();
    if (!probe.is_mounted) {
        mclog::tagWarn(getAppInfo().name, "sd card not mounted");
        return;
    }

    const char* storyRoot = nullptr;
    DIR* root = nullptr;
    int rootError = ENOENT;
    for (const char* candidate : kStoryRoots) {
        root = opendir(candidate);
        if (root) {
            storyRoot = candidate;
            break;
        }
        rootError = errno;
    }
    if (!root) {
        mclog::tagWarn(getAppInfo().name, "story root not found: {}", std::strerror(rootError));
        return;
    }

    while (dirent* entry = readdir(root)) {
        if (entry->d_name[0] == '.') continue;

        const std::string storyPath = std::string(storyRoot) + "/" + entry->d_name + "/" + kStoryFileName;
        StoryData story;
        if (!load_story_from_path(storyPath, story)) {
            mclog::tagWarn(getAppInfo().name, "unable to load {}", storyPath);
            continue;
        }

        _stories.push_back({story.title, story.symbol, storyPath});
    }
    closedir(root);

    // Be tolerant of cards prepared by a PC file manager that places files
    // one level deeper or reports directory entries inconsistently.
    if (_stories.empty()) {
        std::function<void(const std::string&, int)> walk = [&](const std::string& dir, int depth) {
            if (depth > 3) return;
            DIR* current = opendir(dir.c_str());
            if (!current) return;
            while (dirent* entry = readdir(current)) {
                if (entry->d_name[0] == '.') continue;
                const std::string path = dir + "/" + entry->d_name;
                // FATFS can return the 8.3 spelling in upper case even when
                // the PC created a lower-case long name (for example
                // STORY.JSON versus story.json).
                if (is_story_file_name(entry->d_name)) {
                    StoryData story;
                    if (load_story_from_path(path, story)) {
                        _stories.push_back({story.title, story.symbol, path});
                    }
                } else {
                    walk(path, depth + 1);
                }
            }
            closedir(current);
        };
        walk(storyRoot, 0);
    }

    std::sort(_stories.begin(), _stories.end(), [](const StoryEntry& a, const StoryEntry& b) {
        return a.symbol < b.symbol;
    });
    mclog::tagInfo(getAppInfo().name, "found {} stories", _stories.size());
}

bool AppTimeMachine::load_story_from_path(const std::string& path, StoryData& story)
{
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        mclog::tagWarn(getAppInfo().name, "open failed {}: {}", path, std::strerror(errno));
        return false;
    }

    std::fseek(fp, 0, SEEK_END);
    const long size = std::ftell(fp);
    std::rewind(fp);
    if (size <= 0 || size > 16384) {
        std::fclose(fp);
        mclog::tagWarn(getAppInfo().name, "invalid size {} ({})", path, size);
        return false;
    }

    std::string json;
    json.resize(static_cast<size_t>(size));
    const size_t bytesRead = std::fread(json.data(), 1, json.size(), fp);
    std::fclose(fp);
    if (bytesRead != json.size()) {
        mclog::tagWarn(getAppInfo().name, "short read {} ({}/{})", path, bytesRead, json.size());
        return false;
    }

    if (!parse_story_json(json.c_str(), story)) {
        mclog::tagWarn(getAppInfo().name, "JSON validation failed {}", path);
        return false;
    }
    story.path = path;
    story.loadedFromSd = true;
    return true;
}

bool AppTimeMachine::open_selected_story(bool startPlaying)
{
    if (_stories.empty()) return false;

    _selected_story_index = static_cast<int>(clampf(static_cast<float>(_selected_story_index), 0.0f,
                                                     static_cast<float>(_stories.size() - 1)));
    StoryData selected;
    if (!load_story_from_path(_stories[_selected_story_index].path, selected)) {
        scan_story_library();
        return false;
    }

    _story = selected;
    _series.clear();
    build_series();
    if (_series.size() < 2) {
        mclog::tagWarn(getAppInfo().name, "story produced no usable series: {}", selected.path);
        _story = StoryData{};
        return false;
    }
    reset_playback(startPlaying);
    return true;
}

bool AppTimeMachine::parse_story_json(const char* json, StoryData& story) const
{
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;

    auto read_string = [](cJSON* parent, const char* name, std::string& out) {
        cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, name);
        if (cJSON_IsString(item) && item->valuestring) out = item->valuestring;
    };

    auto read_float = [](cJSON* parent, const char* name, float& out) {
        cJSON* item = cJSON_GetObjectItemCaseSensitive(parent, name);
        if (cJSON_IsNumber(item)) out = static_cast<float>(item->valuedouble);
    };

    read_string(root, "title", story.title);
    read_string(root, "symbol", story.symbol);
    read_float(root, "initialInvestment", story.initialInvestment);
    read_float(root, "startYear", story.startYear);
    read_float(root, "endYear", story.endYear);

    cJSON* anchors = cJSON_GetObjectItemCaseSensitive(root, "anchors");
    if (cJSON_IsArray(anchors)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, anchors)
        {
            cJSON* year = cJSON_GetObjectItemCaseSensitive(item, "year");
            cJSON* price = cJSON_GetObjectItemCaseSensitive(item, "price");
            if (!cJSON_IsNumber(year) || !cJSON_IsNumber(price)) continue;

            Anchor anchor;
            anchor.year = static_cast<float>(year->valuedouble);
            anchor.price = static_cast<float>(price->valuedouble);
            read_string(item, "label", anchor.label);
            story.anchors.push_back(anchor);
        }
    }

    cJSON* events = cJSON_GetObjectItemCaseSensitive(root, "events");
    if (cJSON_IsArray(events)) {
        cJSON* item = nullptr;
        cJSON_ArrayForEach(item, events)
        {
            cJSON* year = cJSON_GetObjectItemCaseSensitive(item, "year");
            if (!cJSON_IsNumber(year)) continue;

            EventMarker event;
            event.year = static_cast<float>(year->valuedouble);
            read_string(item, "label", event.label);
            read_string(item, "mood", event.mood);
            if (!event.label.empty()) story.events.push_back(event);
        }
    }

    cJSON_Delete(root);
    return validate_story(story);
}

bool AppTimeMachine::validate_story(StoryData& story) const
{
    // Keep untrusted SD-card data from causing unbounded allocations or
    // non-finite drawing math on the device.
    constexpr size_t kMaxAnchors = 128;
    if (story.anchors.size() < 2 || story.anchors.size() > kMaxAnchors) return false;

    std::sort(story.anchors.begin(), story.anchors.end(), [](const Anchor& a, const Anchor& b) {
        return a.year < b.year;
    });
    std::sort(story.events.begin(), story.events.end(), [](const EventMarker& a, const EventMarker& b) {
        return a.year < b.year;
    });

    constexpr size_t kMaxEventLabelLen = 12;
    for (auto& event : story.events) {
        if (event.label.size() > kMaxEventLabelLen) event.label.resize(kMaxEventLabelLen);
    }

    for (size_t i = 0; i < story.anchors.size(); ++i) {
        if (!std::isfinite(story.anchors[i].year) || !std::isfinite(story.anchors[i].price) ||
            story.anchors[i].price <= 0.0f || story.anchors[i].year < 1900.0f || story.anchors[i].year > 2200.0f)
            return false;
        if (i > 0 && story.anchors[i].year <= story.anchors[i - 1].year) return false;
    }

    // Cap lengths so SD-card-supplied text can never overflow the 204px
    // canvas and wrap onto (and corrupt) neighboring UI elements.
    constexpr size_t kMaxSymbolLen = 10;
    constexpr size_t kMaxTitleLen = 24;
    if (story.symbol.size() > kMaxSymbolLen) story.symbol.resize(kMaxSymbolLen);
    if (story.title.size() > kMaxTitleLen) story.title.resize(kMaxTitleLen);

    if (story.symbol.empty()) story.symbol = "STORY";
    if (story.title.empty()) story.title = story.symbol;
    if (story.initialInvestment <= 0.0f) story.initialInvestment = 1000.0f;

    story.startYear = story.anchors.front().year;
    story.endYear = story.anchors.back().year;
    story.startPrice = story.anchors.front().price;
    return story.endYear > story.startYear;
}

void AppTimeMachine::build_series()
{
    if (!_series.empty()) return;
    if (_story.anchors.size() < 2) return;

    uint32_t seed = 42;
    const size_t count = _story.anchors.size();
    for (size_t i = 0; i + 1 < count; ++i) {
        const Anchor& a = _story.anchors[i];
        const Anchor& b = _story.anchors[i + 1];
        const float span = b.year - a.year;
        if (!std::isfinite(span) || span <= 0.0f || span > 300.0f) {
            _series.clear();
            return;
        }
        const int steps = std::min(4096, std::max(4, static_cast<int>(std::round(span * 7.0f))));
        const float logA = std::log(a.price);
        const float logB = std::log(b.price);
        const float amp = 0.5f * std::abs(logB - logA) + 0.05f;

        for (int s = 0; s < steps; ++s) {
            const float f = static_cast<float>(s) / static_cast<float>(steps);
            const float trend = lerpf(logA, logB, f);
            const float bridge = f * (1.0f - f) * 2.0f;
            const float noise = (rand01(seed) * 2.0f - 1.0f) * amp * bridge;
            _series.push_back({lerpf(a.year, b.year, f), std::exp(trend + noise)});
        }
    }
    _series.push_back({_story.endYear, _story.anchors.back().price});
}

void AppTimeMachine::reset_playback(bool startPlaying)
{
    _playback.currentYear = _story.startYear;
    _playback.currentPrice = _story.startPrice;
    _playback.speed = 1.0f;
    _playback.started = startPlaying;
    _playback.playing = startPlaying;
    _playback.ending = false;
    _playback.finished = false;
    _camera.centerYear = _story.startYear + 2.25f;
    _camera.centerPrice = _story.startPrice;
    _camera.yearWindow = 4.7f;
    _camera.priceWindow = 2.2f;
    _exit_camera = _camera;
    _summary_transition = 0.0f;
    _peak_value = _story.initialInvestment;
}

void AppTimeMachine::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) return;

    if (keyEvent.keyCode == KEY_ENTER) {
        audio::play_random_tone();
        if (_stories.empty()) {
            scan_story_library();
            if (!_stories.empty()) open_selected_story(false);
        } else if (!_playback.started) {
            open_selected_story(true);
        } else if (_playback.finished) {
            reset_playback(true);
        } else {
            _playback.playing = !_playback.playing;
        }
    } else if (keyEvent.keyCode == KEY_GRAVE || keyEvent.keyCode == KEY_ESC) {
        audio::play_random_tone();
        if (_playback.started) {
            reset_playback(false);
        } else {
            close();
        }
    } else if (!_playback.started && (keyEvent.keyCode == KEY_DOWN || keyEvent.keyCode == KEY_DOT)) {
        if (!_stories.empty()) {
            _selected_story_index = (_selected_story_index + 1) % static_cast<int>(_stories.size());
            open_selected_story(false);
        }
    } else if (!_playback.started && (keyEvent.keyCode == KEY_UP || keyEvent.keyCode == KEY_COMMA)) {
        if (!_stories.empty()) {
            _selected_story_index = (_selected_story_index + static_cast<int>(_stories.size()) - 1) %
                                    static_cast<int>(_stories.size());
            open_selected_story(false);
        }
    } else if (keyEvent.keyCode == KEY_COMMA) {
        _playback.speed = clampf(_playback.speed - 0.25f, 0.5f, 3.0f);
    } else if (keyEvent.keyCode == KEY_DOT) {
        _playback.speed = clampf(_playback.speed + 0.25f, 0.5f, 3.0f);
    }
}

void AppTimeMachine::update_playback(float dt)
{
    if (_story.anchors.empty()) return;
    if (_playback.ending) {
        _summary_transition = clampf(_summary_transition + dt / 1.85f, 0.0f, 1.0f);
        if (_summary_transition >= 1.0f) {
            _playback.ending = false;
            _playback.finished = true;
        }
        return;
    }
    if (!_playback.started || !_playback.playing || _playback.finished) return;

    const float kYearsPerSecond = std::max(0.02f, (_story.endYear - _story.startYear) / 58.0f);
    _playback.currentYear += dt * kYearsPerSecond * _playback.speed;
    if (_playback.currentYear >= _story.endYear) {
        _playback.currentYear = _story.endYear;
        _playback.playing = false;
        _playback.ending = true;
        _exit_camera = _camera;
    }
    _playback.currentPrice = price_at_year(_playback.currentYear);

    const float value = _story.initialInvestment * (_playback.currentPrice / _story.startPrice);
    _peak_value = std::max(_peak_value, value);
}

void AppTimeMachine::update_camera(float dt)
{
    if (_story.anchors.empty() || _series.empty()) return;
    if (_playback.ending || _playback.finished) return;
    const float openingCenter = _story.startYear + _camera.yearWindow * 0.5f;
    const float targetYear = (_playback.currentYear < _story.startYear + 2.1f) ? openingCenter : _playback.currentYear;
    _camera.centerYear = smooth_toward(_camera.centerYear, targetYear, 4.2f, dt);

    float localMin = _playback.currentPrice;
    float localMax = _playback.currentPrice;
    const float left = std::max(_story.startYear, _playback.currentYear - 1.05f);
    for (const auto& p : _series) {
        if (p.year >= left && p.year <= _playback.currentYear) {
            localMin = std::min(localMin, p.price);
            localMax = std::max(localMax, p.price);
        }
    }

    const float targetCenterPrice = (localMin + localMax + _playback.currentPrice) / 3.0f;
    const float targetWindow = std::max(1.2f, (localMax - localMin) * 1.65f + _playback.currentPrice * 0.32f);
    _camera.centerPrice = smooth_toward(_camera.centerPrice, targetCenterPrice, 5.0f, dt);
    _camera.priceWindow = smooth_toward(_camera.priceWindow, targetWindow, 2.2f, dt);
}

void AppTimeMachine::render()
{
    if (_stories.empty()) {
        render_empty_library();
    } else if (!_playback.started) {
        render_library();
    } else if (_playback.ending) {
        render_transition();
    } else if (_playback.finished) {
        render_summary();
    } else {
        render_story();
    }
}

void AppTimeMachine::render_empty_library()
{
    auto& canvas = GetHAL().canvas;
    const int w = canvas.width();
    const int h = canvas.height();

    canvas.fillScreen(THEME_COLOR_BG);
    draw_icon(w / 2, 26, 28, TFT_CYAN);

    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas.drawCenterString("TIME MACHINE", w / 2, 51);
    canvas.setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
    canvas.drawCenterString("No stories on SD", w / 2, 75);
    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas.drawCenterString("SD:/APPS/time_machine", w / 2, 98);
    canvas.drawCenterString("Enter rescan   ` back", w / 2, h - 15);
    GetHAL().pushCanvas();
}

void AppTimeMachine::render_library()
{
    auto& canvas = GetHAL().canvas;
    const int w = canvas.width();
    const int h = canvas.height();

    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    canvas.setCursor(5, 3);
    canvas.print("TIME MACHINE");
    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas.setCursor(w - 44, 3);
    canvas.printf("%d", static_cast<int>(_stories.size()));

    const int selected = _selected_story_index;
    const int first = std::max(0, std::min(selected - 1, std::max(0, static_cast<int>(_stories.size()) - 3)));
    for (int row = 0; row < 3 && first + row < static_cast<int>(_stories.size()); ++row) {
        const int index = first + row;
        const int y = 24 + row * 28;
        const bool isSelected = index == selected;
        const uint16_t color = isSelected ? TFT_WHITE : TFT_LIGHTGREY;
        if (isSelected) {
            canvas.drawRoundRect(4, y - 3, w - 8, 24, 3, TFT_CYAN);
            canvas.fillTriangle(10, y + 4, 10, y + 12, 16, y + 8, TFT_CYAN);
        }

        canvas.setTextColor(color, THEME_COLOR_BG);
        canvas.setCursor(22, y);
        canvas.print(_stories[index].symbol.c_str());
        canvas.setTextColor(isSelected ? TFT_CYAN : TFT_DARKGREY, THEME_COLOR_BG);
        canvas.setCursor(72, y);
        // FONT_BASIC (efontCN_16) is 8px/char; cap so titles never reach the
        // canvas edge (w - 72 - 4px margin) and wrap onto the next row.
        const size_t maxTitleChars = static_cast<size_t>(std::max(0, (w - 72 - 4) / 8));
        std::string title = _stories[index].title;
        if (title.size() > maxTitleChars) title = title.substr(0, maxTitleChars);
        canvas.print(title.c_str());
    }

    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas.setCursor(5, h - 15);
    canvas.print(",/. choose");
    canvas.setCursor(w - 78, h - 15);
    canvas.print("Enter play");
    GetHAL().pushCanvas();
}

void AppTimeMachine::render_story()
{
    auto& canvas = GetHAL().canvas;
    const int w = canvas.width();
    const int h = canvas.height();

    const float value = _story.initialInvestment * (_playback.currentPrice / _story.startPrice);
    const float pct = (value / _story.initialInvestment - 1.0f) * 100.0f;
    const float drawdown = _peak_value > 0.0f ? (value / _peak_value - 1.0f) * 100.0f : 0.0f;
    const float prevPrice = price_at_year(std::max(_story.startYear, _playback.currentYear - 0.18f));
    const float velocity = prevPrice > 0.0f ? (_playback.currentPrice / prevPrice - 1.0f) * 100.0f : 0.0f;
    const char* mood = mood_for(pct, drawdown, velocity);
    const bool showMoodFace = std::abs(velocity) >= 16.0f;

    canvas.fillScreen(THEME_COLOR_BG);
    draw_live_chart(4, 20, w - 8, h - 45, mood, showMoodFace);
    draw_status_text(w, h, value, pct);

    canvas.setTextSize(1);
    canvas.setTextColor(_playback.playing ? TFT_DARKGREY : TFT_YELLOW, THEME_COLOR_BG);
    canvas.setCursor(w - 52, h - 12);
    canvas.printf("%s %.2fx", _playback.playing ? "" : "PAUSE", _playback.speed);

    GetHAL().pushCanvas();
}

void AppTimeMachine::render_transition()
{
    auto& canvas = GetHAL().canvas;
    const int w = canvas.width();
    const int h = canvas.height();
    if (_series.empty()) {
        render_library();
        return;
    }
    const float finalPrice = price_at_year(_story.endYear);
    const float value = _story.initialInvestment * (finalPrice / _story.startPrice);
    const float pct = (value / _story.initialInvestment - 1.0f) * 100.0f;

    canvas.fillScreen(THEME_COLOR_BG);
    draw_transition_chart(4, 20, w - 8, h - 45, _summary_transition);

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
    canvas.setCursor(5, 3);
    canvas.print("THE LONG VIEW");
    canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    canvas.setCursor(w - 70, 3);
    canvas.printf("%+.0f%%", pct);
    canvas.setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
    canvas.setCursor(5, h - 15);
    canvas.printf("$%.0f", value);
    GetHAL().pushCanvas();
}

void AppTimeMachine::render_summary()
{
    auto& canvas = GetHAL().canvas;
    const int w = canvas.width();

    if (_series.empty()) {
        render_library();
        return;
    }
    const float finalPrice = price_at_year(_story.endYear);
    const float value = _story.initialInvestment * (finalPrice / _story.startPrice);
    const float pct = (value / _story.initialInvestment - 1.0f) * 100.0f;

    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
    canvas.setCursor(5, 2);
    canvas.print(_story.symbol.c_str());

    draw_summary_chart(5, 18, w - 10, 52);

    canvas.setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
    canvas.setCursor(7, 77);
    canvas.printf("$%.0f -> $%.0f", _story.initialInvestment, value);

    canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    canvas.setCursor(7, 94);
    canvas.printf("+%.0f%% total", pct);

    canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
    canvas.setCursor(7, 111);
    canvas.print(mood_for(pct, 0.0f, 0.0f));

    canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    canvas.setCursor(w - 74, 111);
    canvas.print("Enter replay");

    GetHAL().pushCanvas();
}

void AppTimeMachine::draw_live_chart(int x, int y, int w, int h, const char* mood, bool showMoodFace)
{
    auto& canvas = GetHAL().canvas;
    const float leftYear = _camera.centerYear - _camera.yearWindow * 0.5f;
    const float rightYear = _camera.centerYear + _camera.yearWindow * 0.5f;
    const float minPrice = std::max(0.01f, _camera.centerPrice - _camera.priceWindow * 0.5f);
    const float maxPrice = _camera.centerPrice + _camera.priceWindow * 0.5f;

    canvas.drawFastHLine(x, y + h - 1, w, TFT_DARKGREY);
    canvas.drawFastVLine(x, y, h, TFT_DARKGREY);

    bool havePrev = false;
    int prevX = 0;
    int prevY = 0;
    for (const auto& p : _series) {
        if (p.year > _playback.currentYear) break;
        if (p.year < leftYear - 0.05f) {
            havePrev = false;
            continue;
        }
        if (p.year > rightYear + 0.05f) break;

        const int px = static_cast<int>(std::round(map_year_to_x(p.year, x, w, leftYear, rightYear)));
        const int py = static_cast<int>(std::round(map_price_to_y(p.price, y, h, minPrice, maxPrice)));
        if (havePrev) canvas.drawLine(prevX, prevY, px, py, TFT_GREEN);
        prevX = px;
        prevY = py;
        havePrev = true;
    }

    const int cx = static_cast<int>(std::round(map_year_to_x(_playback.currentYear, x, w, leftYear, rightYear)));
    const int cy = static_cast<int>(std::round(map_price_to_y(_playback.currentPrice, y, h, minPrice, maxPrice)));
    canvas.fillCircle(cx, cy, 4, TFT_WHITE);
    canvas.drawCircle(cx, cy, 6, TFT_RED);

    if (showMoodFace) {
        const int faceX = static_cast<int>(clampf(static_cast<float>(cx + 15), static_cast<float>(x + 11),
                                                   static_cast<float>(x + w - 11)));
        const int preferredFaceY = cy > y + 22 ? cy - 15 : cy + 15;
        const int faceY = static_cast<int>(clampf(static_cast<float>(preferredFaceY), static_cast<float>(y + 11),
                                                   static_cast<float>(y + h - 11)));
        draw_mood_face(faceX, faceY, mood, mood_color(mood));
    }

    draw_event_markers(x, y, w, h, leftYear, rightYear, minPrice, maxPrice);
}

void AppTimeMachine::draw_transition_chart(int x, int y, int w, int h, float progress)
{
    auto& canvas = GetHAL().canvas;
    if (_series.empty()) return;
    const float eased = progress * progress * (3.0f - 2.0f * progress);
    const float exitLeft = _exit_camera.centerYear - _exit_camera.yearWindow * 0.5f;
    const float exitRight = _exit_camera.centerYear + _exit_camera.yearWindow * 0.5f;
    const float exitMin = std::max(0.01f, _exit_camera.centerPrice - _exit_camera.priceWindow * 0.5f);
    const float exitMax = _exit_camera.centerPrice + _exit_camera.priceWindow * 0.5f;

    float globalMin = _series.front().price;
    float globalMax = _series.front().price;
    for (const auto& p : _series) {
        globalMin = std::min(globalMin, p.price);
        globalMax = std::max(globalMax, p.price);
    }
    const float pad = (globalMax - globalMin) * 0.08f;
    globalMin = std::max(0.01f, globalMin - pad);
    globalMax += pad;

    const float leftYear = lerpf(exitLeft, _story.startYear, eased);
    const float rightYear = lerpf(exitRight, _story.endYear, eased);
    const float minPrice = lerpf(exitMin, globalMin, eased);
    const float maxPrice = lerpf(exitMax, globalMax, eased);

    canvas.drawRect(x, y, w, h, TFT_DARKGREY);
    bool havePrev = false;
    int prevX = 0;
    int prevY = 0;
    for (const auto& p : _series) {
        if (p.year < leftYear || p.year > rightYear) continue;
        const int px = static_cast<int>(std::round(map_year_to_x(p.year, x + 1, w - 2, leftYear, rightYear)));
        const int py = static_cast<int>(std::round(map_price_to_y(p.price, y + 1, h - 2, minPrice, maxPrice)));
        if (havePrev) canvas.drawLine(prevX, prevY, px, py, TFT_GREEN);
        prevX = px;
        prevY = py;
        havePrev = true;
    }
}

void AppTimeMachine::draw_summary_chart(int x, int y, int w, int h)
{
    auto& canvas = GetHAL().canvas;
    if (_series.empty()) return;
    float minPrice = _series.front().price;
    float maxPrice = _series.front().price;
    for (const auto& p : _series) {
        minPrice = std::min(minPrice, p.price);
        maxPrice = std::max(maxPrice, p.price);
    }
    const float pad = (maxPrice - minPrice) * 0.08f;
    minPrice = std::max(0.01f, minPrice - pad);
    maxPrice += pad;

    canvas.drawRect(x, y, w, h, TFT_DARKGREY);
    bool havePrev = false;
    int prevX = 0;
    int prevY = 0;
    for (const auto& p : _series) {
        const int px = static_cast<int>(std::round(map_year_to_x(p.year, x + 1, w - 2, _story.startYear, _story.endYear)));
        const int py = static_cast<int>(std::round(map_price_to_y(p.price, y + 1, h - 2, minPrice, maxPrice)));
        if (havePrev) canvas.drawLine(prevX, prevY, px, py, TFT_GREEN);
        prevX = px;
        prevY = py;
        havePrev = true;
    }
}

void AppTimeMachine::draw_event_markers(int x, int y, int w, int h, float leftYear, float rightYear, float minPrice,
                                             float maxPrice)
{
    auto& canvas = GetHAL().canvas;
    for (const auto& e : _story.events) {
        if (e.year > _playback.currentYear || e.year < leftYear || e.year > rightYear) continue;
        const float price = price_at_year(e.year);
        const int ex = static_cast<int>(std::round(map_year_to_x(e.year, x, w, leftYear, rightYear)));
        const int ey = static_cast<int>(std::round(map_price_to_y(price, y, h, minPrice, maxPrice)));
        canvas.drawFastVLine(ex, std::max(y, ey - 9), 7, TFT_YELLOW);
        canvas.setTextSize(1);
        canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
        canvas.setCursor(clampf(ex - 13, x, x + w - 38), clampf(ey - 22, y, y + h - 12));
        canvas.print(e.label.c_str());
    }
}

void AppTimeMachine::draw_status_text(int width, int height, float value, float pct)
{
    auto& canvas = GetHAL().canvas;

    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    canvas.setCursor(5, 3);
    canvas.printf("%.1f", _playback.currentYear);

    canvas.setTextColor(pct >= 0.0f ? TFT_GREEN : TFT_RED, THEME_COLOR_BG);
    canvas.setCursor(width - 70, 3);
    canvas.printf("%+.0f%%", pct);

    canvas.setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
    canvas.setCursor(5, height - 15);
    canvas.printf("$%.0f", value);

}

void AppTimeMachine::draw_icon(int cx, int cy, int size, uint16_t color)
{
    auto& canvas = GetHAL().canvas;
    const int half = size / 2;
    canvas.drawCircle(cx, cy, half, color);
    canvas.drawLine(cx, cy, cx, cy - half + 5, color);
    canvas.drawLine(cx, cy, cx + half - 6, cy + 3, color);
    canvas.drawLine(cx - half + 5, cy + half - 4, cx - 3, cy + 3, TFT_GREEN);
    canvas.drawLine(cx - 3, cy + 3, cx + 4, cy + 7, TFT_GREEN);
    canvas.drawLine(cx + 4, cy + 7, cx + half - 3, cy - 5, TFT_GREEN);
}

void AppTimeMachine::draw_mood_face(int cx, int cy, const char* mood, uint16_t color)
{
    auto& canvas = GetHAL().canvas;
    canvas.drawCircle(cx, cy, 10, color);

    if (std::strcmp(mood, "diamond hands") == 0) {
        canvas.drawFastHLine(cx - 7, cy - 3, 6, color);
        canvas.drawFastHLine(cx + 2, cy - 3, 6, color);
        canvas.drawLine(cx - 1, cy - 3, cx + 2, cy - 3, color);
        canvas.drawFastHLine(cx - 4, cy + 5, 8, color);
        return;
    }

    if (std::strcmp(mood, "euphoria") == 0) {
        canvas.drawLine(cx - 7, cy - 2, cx - 3, cy + 1, color);
        canvas.drawLine(cx - 3, cy + 1, cx - 1, cy - 2, color);
        canvas.drawLine(cx + 1, cy - 2, cx + 4, cy + 1, color);
        canvas.drawLine(cx + 4, cy + 1, cx + 7, cy - 2, color);
        canvas.drawLine(cx - 5, cy + 4, cx, cy + 7, color);
        canvas.drawLine(cx, cy + 7, cx + 5, cy + 4, color);
        return;
    }

    canvas.fillCircle(cx - 4, cy - 3, 1, color);
    canvas.fillCircle(cx + 4, cy - 3, 1, color);
    if (std::strcmp(mood, "panic") == 0) {
        canvas.drawRect(cx - 3, cy + 3, 7, 4, color);
    } else if (std::strcmp(mood, "regret") == 0 || std::strcmp(mood, "nervous") == 0) {
        canvas.drawLine(cx - 5, cy + 6, cx, cy + 3, color);
        canvas.drawLine(cx, cy + 3, cx + 5, cy + 6, color);
    } else if (std::strcmp(mood, "hopeful") == 0) {
        canvas.drawLine(cx - 5, cy + 3, cx, cy + 6, color);
        canvas.drawLine(cx, cy + 6, cx + 5, cy + 3, color);
    } else {
        canvas.drawFastHLine(cx - 4, cy + 5, 9, color);
    }
}

float AppTimeMachine::price_at_year(float year) const
{
    if (_series.empty()) return _story.startPrice;
    if (year <= _series.front().year) return _series.front().price;
    if (year >= _series.back().year) return _series.back().price;

    for (size_t i = 0; i + 1 < _series.size(); ++i) {
        const SeriesPoint& a = _series[i];
        const SeriesPoint& b = _series[i + 1];
        if (year >= a.year && year <= b.year) {
            const float t = (year - a.year) / (b.year - a.year);
            return lerpf(a.price, b.price, t);
        }
    }
    return _series.back().price;
}

const char* AppTimeMachine::mood_for(float pct, float drawdown, float velocity) const
{
    if (drawdown < -55.0f || velocity < -28.0f) return "panic";
    if (drawdown < -35.0f) return "regret";
    if (velocity < -12.0f) return "nervous";
    if (pct > 25000.0f && drawdown > -15.0f) return "diamond hands";
    if (velocity > 25.0f || pct > 12000.0f) return "euphoria";
    if (pct > 1000.0f) return "hopeful";
    if (pct < -10.0f) return "nervous";
    return "holding";
}

uint16_t AppTimeMachine::mood_color(const char* mood) const
{
    if (std::strcmp(mood, "panic") == 0 || std::strcmp(mood, "regret") == 0) return TFT_RED;
    if (std::strcmp(mood, "nervous") == 0) return TFT_ORANGE;
    if (std::strcmp(mood, "euphoria") == 0 || std::strcmp(mood, "diamond hands") == 0) return TFT_YELLOW;
    if (std::strcmp(mood, "hopeful") == 0) return TFT_GREEN;
    return TFT_LIGHTGREY;
}

float AppTimeMachine::map_year_to_x(float year, int x, int w, float leftYear, float rightYear) const
{
    const float t = (year - leftYear) / std::max(0.01f, rightYear - leftYear);
    return x + clampf(t, 0.0f, 1.0f) * static_cast<float>(w - 1);
}

float AppTimeMachine::map_price_to_y(float price, int y, int h, float minPrice, float maxPrice) const
{
    const float t = (price - minPrice) / std::max(0.01f, maxPrice - minPrice);
    return y + (1.0f - clampf(t, 0.0f, 1.0f)) * static_cast<float>(h - 1);
}
