#include "timestomp.h"
#include "logger.h"
#include "utils.h"

#include <Windows.h>

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

// ─── File / directory timestomping ──────────────────────────────────────────

bool applyToPath(const std::string& path) {
    if (!s_baseline.valid) return false;

    // FILE_FLAG_BACKUP_SEMANTICS is required for opening directories.
    HANDLE hFile = CreateFileA(
        path.c_str(), FILE_WRITE_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS,
        nullptr);

    if (hFile == INVALID_HANDLE_VALUE) return false;

    BOOL ok = SetFileTime(hFile,
                          &s_baseline.creation,
                          &s_baseline.lastAccess,
                          &s_baseline.lastWrite);
    CloseHandle(hFile);
    return ok != FALSE;
}

// ─── Registry key timestomping ──────────────────────────────────────────────

bool applyToRegistryKey(void* hKeyHandle) {
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

    // The required structure is a single LARGE_INTEGER (the new write time).
    LARGE_INTEGER li;
    li.LowPart  = s_baseline.lastWrite.dwLowDateTime;
    li.HighPart = static_cast<LONG>(s_baseline.lastWrite.dwHighDateTime);

    long status = pNtSetInfoKey(hKeyHandle, 0 /*KeyWriteTimeInformation*/,
                                &li, sizeof(li));
    return status == 0;   // STATUS_SUCCESS
}

// ─── Batch pass ─────────────────────────────────────────────────────────────

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
                          "Would apply baseline timestamp");
        } else {
            if (applyToPath(p))
                log.logAction(p, ActionStatus::MODIFIED,
                              "Baseline timestamp applied");
            else
                log.logAction(p, ActionStatus::SKIPPED,
                              "Failed to apply timestamp");
        }
    }
}

} // namespace Timestomp
} // namespace Shadow
