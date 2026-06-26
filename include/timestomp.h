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

    /// Applies the baseline timestamps with random jitter (±maxJitterDays)
    /// to a file or directory. Each call produces different but plausible
    /// timestamps, defeating forensic timeline correlation.
    bool applyRandomizedToPath(const std::string& path, int maxJitterDays = 7);

    /// Applies the baseline "Last Written" time to an open registry
    /// key via NtSetInformationKey (ntdll).
    /// @param hKeyHandle  Opened HKEY, passed as void* to keep
    ///                    Windows.h out of this header.
    /// @param jitter      If true, applies jittered timestamp.
    bool applyToRegistryKey(void* hKeyHandle, bool jitter = true);

    /// Post-execution pass: applies randomized baseline timestamps to every
    /// tracked path.  In dry-run mode only logs the intent.
    /// NOTE: SetFileTime only modifies $STANDARD_INFORMATION timestamps.
    ///       Forensic tools that inspect raw $FILE_NAME attributes in the MFT
    ///       can detect the discrepancy. Full MFT manipulation requires kernel-
    ///       mode or raw NTFS access, which is out of scope for user-mode tools.
    void applyToAllPaths(const std::set<std::string>& paths, bool isDryRun);

} // namespace Timestomp
} // namespace Shadow
