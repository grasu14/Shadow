#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Shadow {
namespace PrefetchParser {

    /// Maximum decompression size to prevent OOM from corrupt .pf files (16 MB).
    constexpr uint32_t MAX_DECOMPRESSED_SIZE = 16 * 1024 * 1024;

    /// Structured metadata extracted from a Prefetch file header.
    struct PrefetchInfo {
        std::string filename;           // Original executable name from the header
        uint32_t    version     = 0;    // Prefetch format version (17=XP, 23=Vista/7, 26=8.x, 30=10+)
        uint32_t    runCount    = 0;    // Number of times the executable was launched
        uint64_t    lastRunTime = 0;    // Most recent execution timestamp (FILETIME)
        bool        valid       = false;
    };

    /// Parses structured metadata from a Prefetch file.
    /// Handles both uncompressed (Win7/8) and MAM-compressed (Win10+) formats.
    PrefetchInfo parsePrefetchFile(const std::string& path);

    /// Scans a Windows Prefetch (.pf) file for target strings.
    /// Performs case-insensitive matching against both ASCII and UTF-16LE encodings.
    /// Returns true if the file contains any of the target strings.
    bool containsTargetStrings(const std::string& path, const std::vector<std::string>& targets);

} // namespace PrefetchParser
} // namespace Shadow
