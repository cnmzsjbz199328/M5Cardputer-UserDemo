/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "mp3_decoder.h"
#include <hal/hal.h>
#include <mooncake_log.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <algorithm>
#include <vector>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "thirdparty/minimp3/minimp3.h"

namespace audio {

namespace {
constexpr const char* TAG = "mp3";
// Comfortably larger than the biggest possible MPEG1 layer 3 frame (~2.9KB),
// so a full buffer always contains at least one complete frame.
constexpr size_t INPUT_BUF_SIZE = 8 * 1024;
}  // namespace

bool is_mp3_file(const std::string& fileName)
{
    if (fileName.size() < 4) {
        return false;
    }
    std::string ext = fileName.substr(fileName.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == ".mp3";
}

bool play_mp3_file(const std::string& path, int channel)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        mclog::tagError(TAG, "open {} failed: {}", path, std::strerror(errno));
        return false;
    }

    std::vector<uint8_t> input_buf(INPUT_BUF_SIZE);
    size_t buf_len = 0;
    bool file_eof  = false;

    mp3dec_t mp3d;
    mp3dec_init(&mp3d);
    mp3dec_frame_info_t info;
    static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];

    bool played_any = false;
    bool is_first   = true;

    while (true) {
        if (buf_len < INPUT_BUF_SIZE && !file_eof) {
            size_t space = INPUT_BUF_SIZE - buf_len;
            size_t n     = std::fread(input_buf.data() + buf_len, 1, space, f);
            buf_len += n;
            if (n < space) {
                file_eof = true;
            }
        }

        if (buf_len == 0) {
            break;
        }

        int samples = mp3dec_decode_frame(&mp3d, input_buf.data(), static_cast<int>(buf_len), pcm, &info);

        // No progress possible (buffer holds no complete frame and can't be
        // refilled further) - stop rather than spin.
        if (info.frame_bytes == 0) {
            break;
        }

        std::memmove(input_buf.data(), input_buf.data() + info.frame_bytes, buf_len - info.frame_bytes);
        buf_len -= info.frame_bytes;

        if (samples > 0) {
            played_any = true;
            while (GetHAL().speaker.isPlaying(channel) >= 2) {
                GetHAL().delay(1);
            }
            GetHAL().speaker.playRaw(pcm, samples * info.channels, info.hz, info.channels == 2, 1, channel,
                                      is_first);
            is_first = false;
        }
    }

    std::fclose(f);

    while (GetHAL().speaker.isPlaying(channel)) {
        GetHAL().delay(1);
    }

    if (!played_any) {
        mclog::tagError(TAG, "no valid mp3 frames in {}", path);
    }
    return played_any;
}

}  // namespace audio
