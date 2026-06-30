#include "modules.h"
#include "engine.h"
#include "utils.h"
#include "sqlite_parser.h"
#include "prefetch_parser.h"
#include "config.h"
#include "logger.h"
#include "backup.h"

#include <Windows.h>
#include <ShlObj.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace Shadow {
namespace Modules {

// ─── Safe user profile path resolution ──────────────────────────────────────

static std::string getUserProfilePath() {
    // Use the Windows API instead of deprecated getenv("USERNAME")
    char profilePath[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_PROFILE, nullptr, 0, profilePath))) {
        return std::string(profilePath);
    }
    // Fallback via environment variable (safe API)
    char buf[MAX_PATH]{};
    DWORD len = GetEnvironmentVariableA("USERPROFILE", buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH) {
        return std::string(buf, len);
    }
    return "C:\\Users\\Default";
}

static std::string getAppDataPath() {
    std::string profile = getUserProfilePath();
    return profile + "\\AppData";
}

// ─── Chunked Binary/String Search Helper ────────────────────────────────────
// Reads files in chunks with overlap to catch matches spanning chunk boundaries.
// Prevents OOM on large cache files (hundreds of MB).

static bool fileContainsTargetStrings(const std::string& path,
                                      const std::vector<std::string>& targets) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) return false;

    // Determine the longest target string for overlap calculation
    size_t maxTargetLen = 0;
    for (const auto& t : targets) {
        if (t.size() > maxTargetLen) maxTargetLen = t.size();
    }
    if (maxTargetLen == 0) return false;

    // Pre-lowercase all targets for case-insensitive matching
    std::vector<std::string> targetsLower;
    targetsLower.reserve(targets.size());
    for (const auto& t : targets) {
        std::string lower = t;
        std::transform(lower.begin(), lower.end(), lower.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        targetsLower.push_back(std::move(lower));
    }

    constexpr size_t CHUNK_SIZE = 1 * 1024 * 1024; // 1 MB chunks
    size_t overlapSize = maxTargetLen - 1;

    std::vector<char> buffer(CHUNK_SIZE + overlapSize);
    size_t carryOver = 0;

    while (file) {
        file.read(buffer.data() + carryOver,
                  static_cast<std::streamsize>(CHUNK_SIZE));
        size_t bytesRead = static_cast<size_t>(file.gcount());
        if (bytesRead == 0 && carryOver == 0) break;

        size_t totalBytes = carryOver + bytesRead;

        // Lowercase the chunk for case-insensitive search
        std::string chunk(buffer.data(), totalBytes);
        std::transform(chunk.begin(), chunk.end(), chunk.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        for (const auto& target : targetsLower) {
            if (chunk.find(target) != std::string::npos) {
                return true;
            }
        }

        // Keep the tail as overlap for the next chunk
        if (totalBytes > overlapSize && bytesRead > 0) {
            std::memmove(buffer.data(),
                         buffer.data() + totalBytes - overlapSize,
                         overlapSize);
            carryOver = overlapSize;
        } else {
            carryOver = 0;
        }

        if (bytesRead == 0) break;
    }

    return false;
}

// ─── Browser path discovery ─────────────────────────────────────────────────

struct BrowserProfile {
    std::string name;
    std::string historyPath;       // Path to SQLite History DB
    std::string cachePath;         // Path to cache directory
    bool        isFirefox;         // Uses moz_places schema instead of Chromium
};

static std::vector<BrowserProfile> discoverBrowserProfiles() {
    std::vector<BrowserProfile> profiles;
    std::string appData = getAppDataPath();

    // Chromium-based browsers: all use the same History schema
    struct ChromiumBrowser {
        const char* name;
        const char* relativePath;
    };

    ChromiumBrowser chromiumBrowsers[] = {
        {"Chrome",  "\\Local\\Google\\Chrome\\User Data\\Default"},
        {"Edge",    "\\Local\\Microsoft\\Edge\\User Data\\Default"},
        {"Brave",   "\\Local\\BraveSoftware\\Brave-Browser\\User Data\\Default"},
        {"Opera",   "\\Roaming\\Opera Software\\Opera Stable"},
        {"Opera GX","\\Roaming\\Opera Software\\Opera GX Stable"},
        {"Vivaldi", "\\Local\\Vivaldi\\User Data\\Default"},
    };

    for (const auto& browser : chromiumBrowsers) {
        std::string basePath = appData + browser.relativePath;
        std::string historyPath = basePath + "\\History";
        std::string cachePath = basePath + "\\Cache\\Cache_Data";

        std::error_code ec;
        if (fs::exists(historyPath, ec) || fs::exists(cachePath, ec)) {
            profiles.push_back({browser.name, historyPath, cachePath, false});
        }
    }

    // Firefox: uses a different schema and profile directory structure
    std::string firefoxProfiles = appData + "\\Roaming\\Mozilla\\Firefox\\Profiles";
    std::error_code ec;
    if (fs::exists(firefoxProfiles, ec)) {
        for (const auto& entry : fs::directory_iterator(firefoxProfiles, ec)) {
            if (ec) break;
            if (entry.is_directory()) {
                std::string placesPath = entry.path().string() + "\\places.sqlite";
                std::string cachePath = entry.path().string() + "\\cache2\\entries";
                if (fs::exists(placesPath, ec)) {
                    profiles.push_back({
                        "Firefox (" + entry.path().filename().string() + ")",
                        placesPath, cachePath, true
                    });
                }
            }
        }
    }

    // Discord cache
    std::string discordCache = appData + "\\Roaming\\Discord\\Cache\\Cache_Data";
    if (fs::exists(discordCache, ec)) {
        profiles.push_back({"Discord", "", discordCache, false});
    }

    return profiles;
}

// ─── Local Application Caches ───────────────────────────────────────────────
static void cleanLocalApplicationCaches(Engine& engine) {
    Logger& log = Logger::instance();

    const std::vector<std::string>& cacheTargets = Config::instance().getAppCacheTargets();
    if (cacheTargets.empty()) return;

    auto profiles = discoverBrowserProfiles();

    for (const auto& profile : profiles) {
        // 1. Raw Cache Directories (chunked string search + deletion)
        if (!profile.cachePath.empty()) {
            std::error_code ec;
            if (fs::exists(profile.cachePath, ec)) {
                log.log(LogLevel::DEBUG_DETAIL,
                    "Scanning cache: " + profile.name + " (" + profile.cachePath + ")");

                for (const auto& entry : fs::recursive_directory_iterator(profile.cachePath, ec)) {
                    if (ec) break;
                    if (entry.is_regular_file()) {
                        if (fileContainsTargetStrings(entry.path().string(), cacheTargets)) {
                            engine.processTarget(entry.path().string(), TargetType::FILE_TARGET);
                        }
                    }
                }
            }
        }

        // 2. SQLite Browser History Parsing
        if (!profile.historyPath.empty()) {
            std::error_code ec;
            if (fs::exists(profile.historyPath, ec)) {
                log.log(LogLevel::DEBUG_DETAIL,
                    "Parsing history: " + profile.name + " (" + profile.historyPath + ")");

                if (!engine.IS_DRY_RUN && engine.IS_BACKUP_ENABLED) {
                    BackupManager::instance().backupFile(profile.historyPath);
                }

                bool modified = false;
                if (profile.isFirefox) {
                    modified = SQLiteParser::removeTargetsFromFirefoxHistory(
                        profile.historyPath, cacheTargets, engine.IS_DRY_RUN);
                } else {
                    modified = SQLiteParser::removeTargetsFromHistory(
                        profile.historyPath, cacheTargets, engine.IS_DRY_RUN);
                }

                if (modified) {
                    engine.processTarget(
                        profile.historyPath + "::SQLite_Parsed[" + profile.name + "]",
                        TargetType::SYSTEM_ACTION);
                }
            }
        }
    }
}

// ─── System Artifact Parsing ────────────────────────────────────────────────
static void cleanSystemArtifacts(Engine& engine) {
    Logger& log = Logger::instance();
    std::string prefetchDir = "C:\\Windows\\Prefetch";

    const std::vector<std::string>& artifactTargets = Config::instance().getSystemArtifactTargets();
    if (artifactTargets.empty()) return;

    // ── Prefetch file parsing ───────────────────────────────────────────────
    std::error_code ec;
    if (fs::exists(prefetchDir, ec)) {
        for (const auto& entry : fs::directory_iterator(prefetchDir, ec)) {
            if (ec) break;
            if (entry.is_regular_file() && entry.path().extension() == ".pf") {
                if (PrefetchParser::containsTargetStrings(entry.path().string(), artifactTargets)) {
                    // Also extract metadata for richer logging
                    auto info = PrefetchParser::parsePrefetchFile(entry.path().string());
                    if (info.valid) {
                        log.log(LogLevel::DEBUG_DETAIL,
                            "Prefetch hit: " + info.filename +
                            " (run count: " + std::to_string(info.runCount) +
                            ", version: " + std::to_string(info.version) + ")");
                    }
                    engine.processTarget(entry.path().string(), TargetType::FILE_TARGET);
                }
            }
        }
    }

    // ── BAM Artifact Parsing (case-insensitive) ─────────────────────────────
    std::vector<std::string> userSIDs = Registry::enumerateSubKeys(
        "HKLM", "SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings");

    for (const auto& sid : userSIDs) {
        std::string sidKey = "SYSTEM\\CurrentControlSet\\Services\\bam\\State\\UserSettings\\" + sid;
        std::vector<std::string> bamValues = Registry::enumerateValues("HKLM", sidKey);

        for (const auto& valueName : bamValues) {
            bool isTarget = false;

            // Case-insensitive comparison for BAM value names
            // BAM stores paths like "\Device\HarddiskVolume3\path\to\exe"
            std::string valueNameLower = valueName;
            std::transform(valueNameLower.begin(), valueNameLower.end(),
                           valueNameLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            for (const auto& target : artifactTargets) {
                std::string targetLower = target;
                std::transform(targetLower.begin(), targetLower.end(),
                               targetLower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (valueNameLower.find(targetLower) != std::string::npos) {
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

typedef DWORD(WINAPI* DnsFlushResolverCache_func)();
typedef DWORD(WINAPI* FlushIpNetTable_func)(DWORD dwIfIndex);

static void cleanNetworkArtifacts(Engine& engine) {
    if (engine.IS_DRY_RUN) {
        // We cannot simulate a global flush, so we log pseudo-targets
        engine.processTarget("Network::DNS_Resolver_Cache", TargetType::SYSTEM_ACTION);
        engine.processTarget("Network::ARP_Table", TargetType::SYSTEM_ACTION);
        return;
    }

    // 1. Flush DNS Cache
    HMODULE hDnsApi = LoadLibraryA("dnsapi.dll");
    if (hDnsApi) {
        auto DnsFlushResolverCache = reinterpret_cast<DnsFlushResolverCache_func>(
            GetProcAddress(hDnsApi, "DnsFlushResolverCache"));

        if (DnsFlushResolverCache) {
            DnsFlushResolverCache();
            engine.processTarget("Network::DNS_Resolver_Cache", TargetType::SYSTEM_ACTION);
        }
        FreeLibrary(hDnsApi);
    }

    // 2. Flush ARP Table
    // Brute-force approach: flush interface indices 1 through 64.
    // This is a known robust method when iprtrmib.h headers aren't available,
    // and avoids the complexity of parsing MIB_IFTABLE with its platform-
    // dependent struct alignment.
    HMODULE hIpHlpApi = LoadLibraryA("iphlpapi.dll");
    if (hIpHlpApi) {
        auto FlushIpNetTable = reinterpret_cast<FlushIpNetTable_func>(
            GetProcAddress(hIpHlpApi, "FlushIpNetTable"));

        if (FlushIpNetTable) {
            bool arpFlushed = false;
            for (DWORD i = 1; i <= 64; ++i) {
                if (FlushIpNetTable(i) == NO_ERROR) {
                    arpFlushed = true;
                }
            }
            if (arpFlushed) {
                engine.processTarget("Network::ARP_Table", TargetType::SYSTEM_ACTION);
            }
        }
        FreeLibrary(hIpHlpApi);
    }
}

// ─── Custom Paths ─────────────────────────────────────────────────────────────
static void cleanCustomPaths(Engine& engine) {
    Logger& log = Logger::instance();
    const std::vector<std::string>& customPaths = Config::instance().getCustomPaths();
    
    if (customPaths.empty()) return;

    for (const auto& path : customPaths) {
        std::error_code ec;
        if (fs::exists(path, ec)) {
            if (fs::is_directory(path, ec)) {
                engine.processTarget(path, TargetType::DIRECTORY_TARGET);
            } else {
                engine.processTarget(path, TargetType::FILE_TARGET);
            }
        } else {
            log.log(LogLevel::DEBUG_DETAIL, "Custom path not found: " + path);
        }
    }
}

// ─── Emergency Paths ──────────────────────────────────────────────────────────
static void cleanEmergencyPaths(Engine& engine) {
    Logger& log = Logger::instance();
    const std::vector<std::string>& emergencyPaths = Config::instance().getEmergencyPaths();
    
    if (emergencyPaths.empty()) return;

    for (const auto& path : emergencyPaths) {
        std::error_code ec;
        if (fs::exists(path, ec)) {
            if (fs::is_directory(path, ec)) {
                engine.processTarget(path, TargetType::DIRECTORY_TARGET);
            } else {
                engine.processTarget(path, TargetType::FILE_TARGET);
            }
        } else {
            log.log(LogLevel::DEBUG_DETAIL, "Emergency path not found: " + path);
        }
    }
}

// ─── Registration ───────────────────────────────────────────────────────────
void registerAll(Engine& engine) {
    engine.registerModule(
        "Local Application Caches",
        "Parses browser caches (Chrome, Edge, Brave, Opera, Firefox, Discord) and SQLite history databases.",
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

    engine.registerModule(
        "Custom Paths Cleanup",
        "Deletes specific files and folders defined by the user.",
        cleanCustomPaths
    );

    engine.registerModule(
        "Emergency Paths Cleanup",
        "Deletes specific emergency paths defined in the Panic Config.",
        cleanEmergencyPaths
    );
}

} // namespace Modules
} // namespace Shadow
