#include "utils.h"

#include <Windows.h>
#include <ShlObj.h>         // SHGetFolderPathA
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Shadow {
namespace Utils {

// ─── Console ─────────────────────────────────────────────────────────────────

void setupConsole() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
    SetConsoleTitleA("Shadow v1.0");

    // Enable ANSI / Virtual-Terminal escape sequences (Windows 10+).
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD  mode = 0;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleMode(hOut, &mode)) {
        mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
        SetConsoleMode(hOut, mode);
    }
}

// ─── Filesystem helpers ──────────────────────────────────────────────────────

std::string getDesktopPath() {
    char path[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_DESKTOPDIRECTORY,
                                    nullptr, 0, path))) {
        return std::string(path);
    }
    return ".";   // fallback to working directory
}

// ─── Privilege check ─────────────────────────────────────────────────────────

bool isRunningAsAdmin() {
    BOOL                     isAdmin     = FALSE;
    PSID                     adminGroup  = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2,
                                  SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0,
                                  &adminGroup)) {
        if (!CheckTokenMembership(nullptr, adminGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

// ─── Time formatting ─────────────────────────────────────────────────────────

std::string getTimestamp() {
    auto     now = std::time(nullptr);
    std::tm  buf{};
    localtime_s(&buf, &now);

    std::ostringstream oss;
    oss << std::put_time(&buf, "%H:%M:%S");
    return oss.str();
}

std::string getCurrentDateTimeFormatted() {
    auto     now = std::time(nullptr);
    std::tm  buf{};
    localtime_s(&buf, &now);

    std::ostringstream oss;
    oss << std::put_time(&buf, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// ─── Screen ──────────────────────────────────────────────────────────────────

void clearScreen() {
    // Safe console clear on Windows.
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (hOut != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(hOut, &csbi)) {
        DWORD  cellCount = csbi.dwSize.X * csbi.dwSize.Y;
        DWORD  written   = 0;
        COORD  origin    = {0, 0};
        FillConsoleOutputCharacterA(hOut, ' ', cellCount, origin, &written);
        FillConsoleOutputAttribute(hOut, csbi.wAttributes, cellCount, origin, &written);
        SetConsoleCursorPosition(hOut, origin);
    }
}

void disableQuickEdit() {
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    if (hInput == INVALID_HANDLE_VALUE) return;

    DWORD mode = 0;
    if (GetConsoleMode(hInput, &mode)) {
        mode &= ~ENABLE_QUICK_EDIT_MODE;
        mode |= ENABLE_EXTENDED_FLAGS;
        SetConsoleMode(hInput, mode);
    }
}

} // namespace Utils

namespace Registry {

bool parseRegistryPath(const std::string& fullPath, void*& outRootHKey, std::string& outSubKey, std::string& outValueName) {
    if (fullPath.empty()) return false;

    std::string path = fullPath;
    
    // Determine root HKEY
    if (path.find("HKLM\\") == 0 || path.find("HKEY_LOCAL_MACHINE\\") == 0) {
        outRootHKey = HKEY_LOCAL_MACHINE;
        path = path.substr(path.find('\\') + 1);
    } else if (path.find("HKCU\\") == 0 || path.find("HKEY_CURRENT_USER\\") == 0) {
        outRootHKey = HKEY_CURRENT_USER;
        path = path.substr(path.find('\\') + 1);
    } else if (path.find("HKU\\") == 0 || path.find("HKEY_USERS\\") == 0) {
        outRootHKey = HKEY_USERS;
        path = path.substr(path.find('\\') + 1);
    } else {
        return false;
    }

    // Determine subkey and value
    size_t sep = path.rfind("::");
    if (sep != std::string::npos) {
        outSubKey = path.substr(0, sep);
        outValueName = path.substr(sep + 2);
    } else {
        outSubKey = path;
        outValueName = ""; // It's just a key, no value specified
    }

    return true;
}

std::vector<std::string> enumerateSubKeys(const std::string& rootHKeyString, const std::string& subKey) {
    std::vector<std::string> keys;
    void* rootKey = nullptr;
    std::string unusedSub, unusedVal;
    
    // We can reuse the parser to get the root HKEY
    if (!parseRegistryPath(rootHKeyString + "\\Placeholder::", rootKey, unusedSub, unusedVal)) {
        return keys;
    }

    HKEY hKey;
    if (RegOpenKeyExA(static_cast<HKEY>(rootKey), subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char keyName[MAX_PATH];
        DWORD nameSize = MAX_PATH;
        DWORD index = 0;

        while (RegEnumKeyExA(hKey, index, keyName, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            keys.push_back(keyName);
            nameSize = MAX_PATH;
            index++;
        }
        RegCloseKey(hKey);
    }
    return keys;
}

std::vector<std::string> enumerateValues(const std::string& rootHKeyString, const std::string& subKey) {
    std::vector<std::string> values;
    void* rootKey = nullptr;
    std::string unusedSub, unusedVal;
    
    if (!parseRegistryPath(rootHKeyString + "\\Placeholder::", rootKey, unusedSub, unusedVal)) {
        return values;
    }

    HKEY hKey;
    if (RegOpenKeyExA(static_cast<HKEY>(rootKey), subKey.c_str(), 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char valueName[16383]; // Max value name size
        DWORD nameSize = sizeof(valueName);
        DWORD index = 0;

        while (RegEnumValueA(hKey, index, valueName, &nameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            values.push_back(valueName);
            nameSize = sizeof(valueName);
            index++;
        }
        RegCloseKey(hKey);
    }
    return values;
}

} // namespace Registry

} // namespace Shadow
