#pragma once
#include <string>
#include <vector>

namespace Shadow {
namespace Utils {

// ─── Console ─────────────────────────────────────────────────────────────────
void setupConsole();

// ─── Filesystem helpers ──────────────────────────────────────────────────────
std::string getDesktopPath();
std::string openFileDialog(const wchar_t* filterName = L"Executables", const wchar_t* filterExt = L"*.exe");
std::string openFolderDialog();

// ─── Processes ───────────────────────────────────────────────────────────────
std::vector<std::string> getRunningProcesses();

    /// Returns true if the process has Administrator privileges.
    bool isRunningAsAdmin();

    /// Returns a time-only stamp  e.g. "14:32:07"
    std::string getTimestamp();

    /// Returns a full date-time  e.g. "2026-06-25 14:32:07"
    std::string getCurrentDateTimeFormatted();

    /// Clears the console window.
    void clearScreen();

    // Disables console quick-edit mode to prevent pausing execution.
    void disableQuickEdit();

} // namespace Utils

namespace Registry {
    
    // Splits "HKEY_LOCAL_MACHINE\Path\To\Key::ValueName" into pieces.
    // Returns true if successfully parsed.
    bool parseRegistryPath(const std::string& fullPath, void*& outRootHKey, std::string& outSubKey, std::string& outValueName);

    // Enumerates all subkey names under a given path.
    std::vector<std::string> enumerateSubKeys(const std::string& rootHKeyString, const std::string& subKey);

    // Enumerates all value names under a given path.
    std::vector<std::string> enumerateValues(const std::string& rootHKeyString, const std::string& subKey);

} // namespace Registry

} // namespace Shadow
