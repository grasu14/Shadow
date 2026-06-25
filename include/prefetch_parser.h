#pragma once
#include <string>
#include <vector>

namespace Shadow {
namespace PrefetchParser {

    /// Scans a Windows Prefetch (.pf) file for target strings.
    /// Safely handles both uncompressed and Windows 10+ MAM-compressed prefetch files.
    /// Returns true if the file contains any of the target strings.
    bool containsTargetStrings(const std::string& path, const std::vector<std::string>& targets);

} // namespace PrefetchParser
} // namespace Shadow
