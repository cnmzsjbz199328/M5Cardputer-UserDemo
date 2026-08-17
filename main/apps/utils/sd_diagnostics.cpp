#include "sd_diagnostics.h"

#include <hal.h>
#include <cerrno>
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
}  // namespace

namespace SdCardDiagnostics {

std::vector<std::string> scan()
{
    std::vector<std::string> lines;
    const auto probe = GetHAL().sdCardProbe();
    lines.emplace_back(probe.is_mounted ? "MOUNT: OK" : "MOUNT: FAIL");
    if (!probe.is_mounted) {
        return lines;
    }

    add_dir_result(lines, "/sdcard");
    add_dir_result(lines, "/sdcard/APPS");
    add_dir_result(lines, "/sdcard/APPS/TIME_M~1");
    add_dir_result(lines, "/sdcard/APPS/TIME_M~1/APPLE");
    add_dir_result(lines, "/sdcard/APPS/TIME_M~1/BITCOIN");
    add_dir_result(lines, "/sdcard/APPS/TIME_M~1/GAMESTOP");
    add_dir_result(lines, "/sdcard/APPS/TIME_M~1/NVIDIA");
    add_dir_result(lines, "/sdcard/APPS/TIME_M~1/TESLA");
    for (const char* path : kFiles) {
        struct stat st {};
        if (stat(path, &st) != 0) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "F FAIL %.24s (%.35s)", std::strrchr(path, '/') + 1,
                          std::strerror(errno));
            lines.emplace_back(buf);
            continue;
        }
        FILE* fp = std::fopen(path, "rb");
        if (!fp) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "F OPEN %.24s (%.35s)", std::strrchr(path, '/') + 1,
                          std::strerror(errno));
            lines.emplace_back(buf);
            continue;
        }
        char first[16] = {};
        const size_t n = std::fread(first, 1, sizeof(first) - 1, fp);
        std::fclose(fp);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "F OK %.24s %dB", std::strrchr(path, '/') + 1, static_cast<int>(n));
        lines.emplace_back(buf);
    }
    return lines;
}

}  // namespace SdCardDiagnostics
