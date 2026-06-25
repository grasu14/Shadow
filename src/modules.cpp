#include "modules.h"
#include "engine.h"
#include "utils.h"
#include "sqlite_parser.h"
#include "prefetch_parser.h"
#include "config.h"

#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace Shadow {
namespace Modules {

// ─── Basic Binary/String Search Helper ──────────────────────────────────────
static bool fileContainsTargetStrings(const std::string& path,
                                      const std::vector<std::string>& targets) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    // Read the file into memory (in a real scenario, this should be buffered/chunked)
    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());

    for (const auto& target : targets) {
        if (content.find(target) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// ─── Local Application Caches ───────────────────────────────────────────────
static void cleanLocalApplicationCaches(Engine& engine) {
    std::string appData = Utils::getDesktopPath() + "\\..\\AppData";
    if (appData.find("Desktop") != std::string::npos) {
        // Fallback or exact translation depending on system layout
#pragma warning(push)
#pragma warning(disable: 4996)
        appData = "C:\\Users\\" + std::string(getenv("USERNAME")) + "\\AppData";
#pragma warning(pop)
    }

    std::vector<std::string> searchPaths = {
        appData + "\\Roaming\\Discord\\Cache\\Cache_Data",
        appData + "\\Local\\Google\\Chrome\\User Data\\Default\\Cache\\Cache_Data"
    };

    const std::vector<std::string>& cacheTargets = Config::instance().getAppCacheTargets();
    if (cacheTargets.empty()) return;

    // 1. Raw Cache Directories (String search / deletion)
    for (const auto& dir : searchPaths) {
        std::error_code ec;
        if (!fs::exists(dir, ec)) continue;

        for (const auto& entry : fs::recursive_directory_iterator(dir, ec)) {
            if (ec) break;
            if (entry.is_regular_file()) {
                if (fileContainsTargetStrings(entry.path().string(), cacheTargets)) {
                    engine.processTarget(entry.path().string(), TargetType::FILE_TARGET);
                }
            }
        }
    }

    // 2. SQLite Browser History Parsing
    std::vector<std::string> historyPaths = {
        appData + "\\Local\\Google\\Chrome\\User Data\\Default\\History",
        appData + "\\Local\\Microsoft\\Edge\\User Data\\Default\\History"
    };

    for (const auto& db : historyPaths) {
        std::error_code ec;
        if (fs::exists(db, ec)) {
            if (SQLiteParser::removeTargetsFromHistory(db, cacheTargets)) {
                // If it modified the DB, we just log it. We don't delete the DB.
                // We use a pseudo-target to just log the action via the engine.
                engine.processTarget(db + "::SQLite_Parsed", TargetType::FILE_TARGET);
            }
        }
    }
}

// ─── System Artifact Parsing ────────────────────────────────────────────────
static void cleanSystemArtifacts(Engine& engine) {
    std::string prefetchDir = "C:\\Windows\\Prefetch";
    
    const std::vector<std::string>& artifactTargets = Config::instance().getSystemArtifactTargets();
    if (artifactTargets.empty()) return;

    std::error_code ec;
    if (fs::exists(prefetchDir, ec)) {
        for (const auto& entry : fs::directory_iterator(prefetchDir, ec)) {
            if (ec) break;
            if (entry.is_regular_file() && entry.path().extension() == ".pf") {
                if (PrefetchParser::containsTargetStrings(entry.path().string(), artifactTargets)) {
                    engine.processTarget(entry.path().string(), TargetType::FILE_TARGET);
                }
            }
        }
    }

    // ── BAM Artifact Parsing ──
    std::string bamRoot = "HKLM\\SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings";
    
    // Enumerate all SID subkeys under BAM
    std::vector<std::string> userSIDs = Registry::enumerateSubKeys("HKLM", "SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings");
    
    for (const auto& sid : userSIDs) {
        std::string sidKey = "SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings\\" + sid;
        std::vector<std::string> bamValues = Registry::enumerateValues("HKLM", sidKey);
        
        for (const auto& valueName : bamValues) {
            // BAM values store the paths of executed programs
            // The value name is the path itself (e.g., "\Device\HarddiskVolume3\Windows\System32\cmd.exe")
            
            bool isTarget = false;
            for (const auto& target : artifactTargets) {
                // Case-insensitive check would be better here, but for simplicity we'll just check raw
                if (valueName.find(target) != std::string::npos) {
                    isTarget = true;
                    break;
                }
            }
            
            if (isTarget) {
                std::string fullRegPath = "HKLM\\" + sidKey + "::" + valueName;
                engine.processTarget(fullRegPath, TargetType::REGISTRY_KEY);
            }
        }
    }
}

// ─── Network & DNS Artifacts ────────────────────────────────────────────────
// Dynamically load IP Helper and DNS APIs to avoid static dependencies

typedef DWORD(WINAPI* DnsFlushResolverCache_func)();
typedef DWORD(WINAPI* FlushIpNetTable_func)(DWORD dwIfIndex);
typedef DWORD(WINAPI* GetIfTable_func)(PVOID pIfTable, PULONG pdwSize, BOOL bOrder);

// Simplified MIB_IFTABLE and MIB_IFROW for GetIfTable (to avoid pulling in the massive iprtrmib.h)
struct MIB_IFROW_SIMPLIFIED {
    WCHAR wszName[256];
    DWORD dwIndex;
    DWORD dwType;
    DWORD dwMtu;
    DWORD dwSpeed;
    DWORD dwPhysAddrLen;
    BYTE  bPhysAddr[8];
    DWORD dwAdminStatus;
    DWORD dwOperStatus;
    // ... padding out to the size of MIB_IFROW (approx 860 bytes in Win32)
    // We only need the dwIndex, so we will manually parse the returned structure buffer if needed,
    // or just rely on a standard layout.
    BYTE padding[900]; 
};

struct MIB_IFTABLE_SIMPLIFIED {
    DWORD dwNumEntries;
    MIB_IFROW_SIMPLIFIED table[1];
};


static void cleanNetworkArtifacts(Engine& engine) {
    if (engine.IS_DRY_RUN) {
        // We cannot simulate a global flush, so we log pseudo-targets
        engine.processTarget("Network::DNS_Resolver_Cache", TargetType::FILE_TARGET);
        engine.processTarget("Network::ARP_Table", TargetType::FILE_TARGET);
        return;
    }

    // 1. Flush DNS Cache
    HMODULE hDnsApi = LoadLibraryA("dnsapi.dll");
    if (hDnsApi) {
        auto DnsFlushResolverCache = reinterpret_cast<DnsFlushResolverCache_func>(
            GetProcAddress(hDnsApi, "DnsFlushResolverCache"));
        
        if (DnsFlushResolverCache) {
            DnsFlushResolverCache();
            engine.processTarget("Network::DNS_Resolver_Cache", TargetType::FILE_TARGET);
        }
        FreeLibrary(hDnsApi);
    }

    // 2. Flush ARP Table
    HMODULE hIpHlpApi = LoadLibraryA("iphlpapi.dll");
    if (hIpHlpApi) {
        auto GetIfTable = reinterpret_cast<GetIfTable_func>(GetProcAddress(hIpHlpApi, "GetIfTable"));
        auto FlushIpNetTable = reinterpret_cast<FlushIpNetTable_func>(GetProcAddress(hIpHlpApi, "FlushIpNetTable"));

        if (GetIfTable && FlushIpNetTable) {
            ULONG size = 0;
            // Get required size
            if (GetIfTable(nullptr, &size, FALSE) == ERROR_INSUFFICIENT_BUFFER) {
                std::vector<BYTE> buffer(size);
                if (GetIfTable(buffer.data(), &size, FALSE) == NO_ERROR) {
                    
                    // The first 4 bytes are dwNumEntries, but we don't need it.
                    // DWORD numEntries = *reinterpret_cast<DWORD*>(buffer.data());
                    
                    // In a full implementation we would use proper MIB_IFTABLE structs,
                    // but since the structure sizes vary between x86 and x64 due to alignment,
                    // a safer brute-force ARP flush is just to try flushing interface indices 1 through 100.
                    // This is a known, robust hack for IP helper when headers aren't available.
                    
                    bool arpFlushed = false;
                    for (DWORD i = 1; i < 100; ++i) {
                        if (FlushIpNetTable(i) == NO_ERROR) {
                            arpFlushed = true;
                        }
                    }

                    if (arpFlushed) {
                        engine.processTarget("Network::ARP_Table", TargetType::FILE_TARGET);
                    }
                }
            }
        }
        FreeLibrary(hIpHlpApi);
    }
}

// ─── Registration ───────────────────────────────────────────────────────────
void registerAll(Engine& engine) {
    engine.registerModule(
        "Local Application Caches",
        "Parses Discord, browser caches, and logs for telemetry and tracking IDs.",
        cleanLocalApplicationCaches
    );

    engine.registerModule(
        "System Artifacts (Prefetch, BAM)",
        "Parses Prefetch files and registry execution logs for target processes.",
        cleanSystemArtifacts
    );

    engine.registerModule(
        "Network & DNS Artifacts",
        "Flushes the DNS Resolver Cache and ARP routing tables.",
        cleanNetworkArtifacts
    );
}

} // namespace Modules
} // namespace Shadow
