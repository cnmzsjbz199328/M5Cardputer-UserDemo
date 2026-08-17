#pragma once

#include <string>
#include <vector>

// Shared SD card diagnostics scan, used by AppSdcard's diagnostics page
// (and formerly AppSdDiag). Returns a flat list of human-readable result
// lines: mount status, directory listings, and per-story-file read checks.
namespace SdCardDiagnostics {
std::vector<std::string> scan();
}
