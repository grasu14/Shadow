#pragma once

#include <set>
#include <string>

namespace Shadow {
namespace Timestomp {

    /// Reads and caches creation/access/write timestamps from
    /// %WINDIR%\explorer.exe as the baseline reference.
    bool initialize();

    /// Applies the baseline timestamps to a file or directory,
    /// eliminating the modification cluster left by cleaning.
    bool applyToPath(const std::string& path);

    /// Applies the baseline "Last Written" time to an open registry
    /// key via NtSetInformationKey (ntdll).
    /// @param hKeyHandle  Opened HKEY, passed as void* to keep
    ///                    Windows.h out of this header.
    bool applyToRegistryKey(void* hKeyHandle);

    /// Post-execution pass: applies baseline timestamps to every
    /// tracked path.  In dry-run mode only logs the intent.
    void applyToAllPaths(const std::set<std::string>& paths, bool isDryRun);

} // namespace Timestomp
} // namespace Shadow
