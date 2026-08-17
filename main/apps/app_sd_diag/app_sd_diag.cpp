#include "app_sd_diag.h"

#include <hal.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <cerrno>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>

namespace {
constexpr const char* kFiles[] = {
    "/sdcard/APPS/TIME_M~1/APPLE/STORY.JSON",
    "/sdcard/APPS/TIME_M~1/BITCOIN/STORY.JSON",
    "/sdcard/APPS/TIME_M~1/GAMESTOP/STORY.JSON",
    "/sdcard/APPS/TIME_M~1/NVIDIA/STORY.JSON",
    "/sdcard/APPS/TIME_M~1/TESLA/STORY.JSON",
};

void add_dir_result(std::vector<std::string>& lines, const char* path)
{
    DIR* dir = opendir(path);
    if (!dir) {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "D %.45s: %.45s", path, std::strerror(errno));
        lines.emplace_back(buf);
        return;
    }
    int count = 0;
    while (dirent* item = readdir(dir)) {
        if (item->d_name[0] != '.') {
            ++count;
            if (count <= 6) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "  %.70s type=%d", item->d_name, item->d_type);
                lines.emplace_back(buf);
            }
        }
    }
    closedir(dir);
    char buf[128];
    std::snprintf(buf, sizeof(buf), "D %.45s: OK (%d)", path, count);
    lines.emplace_back(buf);
}
}

AppSdDiag::AppSdDiag()
{
    setAppInfo().name = "SDDiag";
}

AppSdDiag::~AppSdDiag() = default;

void AppSdDiag::onOpen()
{
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& event) { handle_key(event); });
    run_probe();
    _last_probe_ms = GetHAL().millis();
}

void AppSdDiag::onRunning()
{
    if (GetHAL().millis() - _last_probe_ms >= 3000) {
        run_probe();
        _last_probe_ms = GetHAL().millis();
    }
    if (GetHAL().homeButton.wasClicked()) close();
}

void AppSdDiag::onClose()
{
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }
}

void AppSdDiag::handle_key(const Keyboard::KeyEvent_t& event)
{
    if (!event.state || event.isModifier) return;
    if (event.keyCode == KEY_ENTER) run_probe();
    else if (event.keyCode == KEY_UP) {
        if (_page > 0) --_page;
        render();
    } else if (event.keyCode == KEY_DOWN) {
        const int pages = std::max(1, (static_cast<int>(_lines.size()) + 3) / 4);
        if (_page + 1 < pages) ++_page;
        render();
    }
    else if (event.keyCode == KEY_ESC || event.keyCode == KEY_GRAVE) close();
}

void AppSdDiag::run_probe()
{
    _lines.clear();
    _page = 0;
    const auto probe = GetHAL().sdCardProbe();
    _lines.emplace_back(probe.is_mounted ? "MOUNT: OK" : "MOUNT: FAIL");
    if (!probe.is_mounted) {
        _lines.emplace_back("Enter retry   Esc back");
        render();
        return;
    }

    add_dir_result(_lines, "/sdcard");
    add_dir_result(_lines, "/sdcard/APPS");
    add_dir_result(_lines, "/sdcard/APPS/TIME_M~1");
    add_dir_result(_lines, "/sdcard/APPS/TIME_M~1/APPLE");
    add_dir_result(_lines, "/sdcard/APPS/TIME_M~1/BITCOIN");
    add_dir_result(_lines, "/sdcard/APPS/TIME_M~1/GAMESTOP");
    add_dir_result(_lines, "/sdcard/APPS/TIME_M~1/NVIDIA");
    add_dir_result(_lines, "/sdcard/APPS/TIME_M~1/TESLA");
    for (const char* path : kFiles) {
        struct stat st {};
        if (stat(path, &st) != 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "F FAIL %.24s (%.35s)", std::strrchr(path, '/') + 1,
                          std::strerror(errno));
            _lines.emplace_back(buf);
            continue;
        }
        FILE* fp = std::fopen(path, "rb");
        if (!fp) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "F OPEN %.24s (%.35s)", std::strrchr(path, '/') + 1,
                          std::strerror(errno));
            _lines.emplace_back(buf);
            continue;
        }
        char first[16] = {};
        const size_t n = std::fread(first, 1, sizeof(first) - 1, fp);
        std::fclose(fp);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "F OK %.24s %dB", std::strrchr(path, '/') + 1, static_cast<int>(n));
        _lines.emplace_back(buf);
    }
    _lines.emplace_back("Enter retry   Esc back");
    for (const auto& line : _lines) mclog::tagInfo("SDDiag", "{}", line);
    render();
}

void AppSdDiag::render()
{
    auto& canvas = GetHAL().canvas;
    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    canvas.setCursor(2, 2);
    canvas.print("SD DIAGNOSTIC");
    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    const int first = _page * 4;
    const int last = std::min(first + 4, static_cast<int>(_lines.size()));
    int y = 24;
    for (int i = first; i < last; ++i) {
        const auto& line = _lines[i];
        if (y > canvas.height() - 5) break;
        std::string shown = line;
        if (shown.size() > 25) shown.resize(25);
        canvas.setCursor(2, y);
        canvas.print(shown.c_str());
        y += 20;
    }
    const int pages = std::max(1, (static_cast<int>(_lines.size()) + 3) / 4);
    canvas.setCursor(2, canvas.height() - 12);
    canvas.printf("Page %d/%d  Up/Down", _page + 1, pages);
    GetHAL().pushCanvas();
}
