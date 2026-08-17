/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <string>

namespace audio {

/**
 * @brief True if fileName ends in ".mp3" (case-insensitive).
 */
bool is_mp3_file(const std::string& fileName);

/**
 * @brief Streams an MP3 file from disk, decoding it with the bundled minimp3
 *        decoder, and plays it back on the given speaker channel. Blocks
 *        until playback finishes.
 *
 * @param path path to the .mp3 file
 * @param channel speaker virtual channel to play on (see Speaker_Class::playRaw)
 * @return false if the file couldn't be opened or no valid MP3 frame was
 *         ever decoded
 */
bool play_mp3_file(const std::string& path, int channel = -1);

}  // namespace audio
