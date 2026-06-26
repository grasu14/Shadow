#include "timestomp.h"
#include "logger.h"
#include "utils.h"

#include <Windows.h>
#include <bcrypt.h>

namespace Shadow {
namespace Timestomp {

// ─── Cached baseline timestamps ─────────────────────────────────────────────

struct BaselineTimestamps {
    FILETIME creation;
    FILETIME lastAccess;
    FILETIME lastWrite;
    bool     valid = false;
};

static BaselineTimestamps s_baseline;
static bool               s_initialized = false;

// ─── CSPRNG jitter helper ───────────────────────────────────────────────────

/// Generates a cryptographically random 32-bit integer in [0, maxVal)
/// using BCryptGenRandom (available on all supported Windows versions).
static uint32_t secureRandom(uint32_t maxVal) {
    if (maxVal <= 1) return 0;

    uint32_t rnd = 0;
    NTSTATUS status = BCryptGenRandom(
        nullptr,
        reinterpret_cast<PUCHAR>(&rnd),
        sizeof(rnd),
        BCRYPT_USE_SYSTEM_PREFERRED_RNG);

    if (status != 0) {
        // Fallback: use high-performance counter as entropy source
        LARGE_INTEGER counter;
        QueryPerformanceCounter(&counter);
        rnd = static_cast<uint32_t>(counter.QuadPart);
    }

    return rnd % maxVal;
}

/// Applies a random offset of ±maxJitterDays to a FILETIME.
/// FILETIME units are 100-nanosecond intervals.
static FILETIME jitterFiletime(const FILETIME& base, int maxJitterDays) {
    ULARGE_INTEGER uli;
    uli.LowPart  = base.dwLowDateTime;
    uli.HighPart = base.dwHighDateTime;

    // 1 day = 24 * 60 * 60 * 10,000,000  =  864,000,000,000  100ns intervals
    constexpr uint64_t TICKS_PER_DAY = 864000000000ULL;
    // Add sub-day granularity for more natural-looking jitter
    constexpr uint64_t TICKS_PER_HOUR = 36000000000ULL;

    // Generate random day offset in [-maxJitterDays, +maxJitterDays]
    int dayRange = maxJitterDays * 2 + 1;
    int dayOffset = static_cast<int>(secureRandom(static_cast<uint32_t>(dayRange))) - maxJitterDays;

    // Generate random hour offset [0, 23]
    uint32_t hourOffset = secureRandom(24);

    int64_t totalOffset = static_cast<int64_t>(dayOffset) * static_cast<int64_t>(TICKS_PER_DAY)
                        + static_cast<int64_t>(hourOffset) * static_cast<int64_t>(TICKS_PER_HOUR);

    // Prevent underflow below epoch
    int64_t result = static_cast<int64_t>(uli.QuadPart) + totalOffset;
    if (result < 0) result = static_cast<int64_t>(uli.QuadPart);  // Clamp to baseline

    uli.QuadPart = static_cast<uint64_t>(result);

    FILETIME ft;
    ft.dwLowDateTime  = uli.LowPart;
    ft.dwHighDateTime = uli.HighPart;
    return ft;
}

// ─── Initialization ─────────────────────────────────────────────────────────

bool initialize() {
    if (s_initialized) return s_baseline.valid;

    char winDir[MAX_PATH]{};
    GetWindowsDirectoryA(winDir, MAX_PATH);
    std::string explorerPath = std::string(winDir) + "\\explorer.exe";

    HANDLE hFile = CreateFileA(
        explorerPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, 0, nullptr);

    if (hFile != INVALID_HANDLE_VALUE) {
        if (GetFileTime(hFile, &s_baseline.creation,
                        &s_baseline.lastAccess, &s_baseline.lastWrite)) {
            s_baseline.valid = true;
        }
        CloseHandle(hFile);
    }

    s_initialized = true;

    Logger::instance().log(
        s_baseline.valid ? LogLevel::DEBUG_DETAIL : LogLevel::WARNING,
        std::string("Timestomp: baseline from explorer.exe ") +
        (s_baseline.valid ? "loaded successfully" : "FAILED to load"));

    return s_baseline.valid;
}

// ─── File / directory timestomping (exact baseline) ─────────────────────────

bool applyToPath(const std::string& path) {
    if (!s_baseline.valid) return false;

    HANDLE hFile = CreateFileA(
        path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        Logger::instance().log(LogLevel::DEBUG_DETAIL,
            "Timestomp: failed to open '" + path +
            "' for timestomping (error " + std::to_string(GetLastError()) + ")");
        return false;
    }

    BOOL ok = SetFileTime(hFile,
                          &s_baseline.creation,
                          &s_baseline.lastAccess,
                          &s_baseline.lastWrite);

    if (!ok) {
        Logger::instance().log(LogLevel::DEBUG_DETAIL,
            "Timestomp: SetFileTime failed on '" + path +
            "' (error " + std::to_string(GetLastError()) + ")");
    }

    CloseHandle(hFile);
    return ok != FALSE;
}

// ─── File / directory timestomping (with jitter) ────────────────────────────

bool applyRandomizedToPath(const std::string& path, int maxJitterDays) {
    if (!s_baseline.valid) return false;

    HANDLE hFile = CreateFileA(
        path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) {
        Logger::instance().log(LogLevel::DEBUG_DETAIL,
            "Timestomp: failed to open '" + path +
            "' for timestomping (error " + std::to_string(GetLastError()) + ")");
        return false;
    }

    // Generate unique jittered timestamps for this file
    FILETIME jCreation   = jitterFiletime(s_baseline.creation, maxJitterDays);
    FILETIME jLastAccess = jitterFiletime(s_baseline.lastAccess, maxJitterDays);
    FILETIME jLastWrite  = jitterFiletime(s_baseline.lastWrite, maxJitterDays);

    BOOL ok = SetFileTime(hFile, &jCreation, &jLastAccess, &jLastWrite);

    if (!ok) {
        Logger::instance().log(LogLevel::DEBUG_DETAIL,
            "Timestomp: SetFileTime failed on '" + path +
            "' (error " + std::to_string(GetLastError()) + ")");
    }

    CloseHandle(hFile);
    return ok != FALSE;
}

// ─── Registry key timestomping ──────────────────────────────────────────────

bool applyToRegistryKey(void* hKeyHandle, bool jitter) {
    if (!s_baseline.valid || !hKeyHandle) return false;

    // Dynamically resolve NtSetInformationKey from ntdll.dll.
    // KeySetInformationClass 0 = KeyWriteTimeInformation.
    using NtSetInformationKeyFn = long (__stdcall*)(
        void*           KeyHandle,
        int             KeySetInformationClass,
        void*           KeySetInformation,
        unsigned long   KeySetInformationLength
    );

    static NtSetInformationKeyFn pNtSetInfoKey = nullptr;
    static bool s_resolved = false;

    if (!s_resolved) {
        HMODULE ntdll = GetModuleHandleA("ntdll.dll");
        if (ntdll)
            pNtSetInfoKey = reinterpret_cast<NtSetInformationKeyFn>(
                GetProcAddress(ntdll, "NtSetInformationKey"));
        s_resolved = true;
    }

    if (!pNtSetInfoKey) return false;

    FILETIME writeTime = s_baseline.lastWrite;
    if (jitter) {
        writeTime = jitterFiletime(s_baseline.lastWrite, 7);
    }

    // The required structure is a single LARGE_INTEGER (the new write time).
    LARGE_INTEGER li;
    li.LowPart  = writeTime.dwLowDateTime;
    li.HighPart = static_cast<LONG>(writeTime.dwHighDateTime);

    long status = pNtSetInfoKey(hKeyHandle, 0 /*KeyWriteTimeInformation*/,
                                &li, sizeof(li));
    return status == 0;   // STATUS_SUCCESS
}

// ─── Batch pass (uses jittered timestamps by default) ───────────────────────

void applyToAllPaths(const std::set<std::string>& paths, bool isDryRun) {
    Logger& log = Logger::instance();

    if (paths.empty()) return;

    if (!initialize()) {
        log.log(LogLevel::WARNING,
                "Timestomp: could not read baseline – skipping pass.");
        return;
    }

    log.log(LogLevel::INFO, "");
    log.log(LogLevel::INFO, "--- Timestomping modified paths ---");

    for (const auto& p : paths) {
        if (isDryRun) {
            log.logAction(p, ActionStatus::WOULD_MODIFY,
                          "Would apply jittered baseline timestamp");
        } else {
            // Use randomized timestomping (±7 days) to defeat timeline correlation
            if (applyRandomizedToPath(p))
                log.logAction(p, ActionStatus::MODIFIED,
                              "Jittered baseline timestamp applied");
            else
                log.logAction(p, ActionStatus::SKIPPED,
                              "Failed to apply timestamp");
        }
    }
}

} // namespace Timestomp
} // namespace Shadow
