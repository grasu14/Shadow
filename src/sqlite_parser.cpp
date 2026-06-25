#include "sqlite_parser.h"
#include <Windows.h>
#include <iostream>

namespace Shadow {
namespace SQLiteParser {

typedef void* sqlite3_ptr;
typedef int (*sqlite3_open_func)(const char *filename, sqlite3_ptr *ppDb);
typedef int (*sqlite3_exec_func)(sqlite3_ptr, const char *sql, int (*callback)(void*,int,char**,char**), void *, char **errmsg);
typedef int (*sqlite3_close_func)(sqlite3_ptr);

bool removeTargetsFromHistory(const std::string& dbPath, const std::vector<std::string>& targets) {
    if (targets.empty()) return false;

    HMODULE hSqlite = LoadLibraryA("winsqlite3.dll");
    if (!hSqlite) return false; // Natively available on Win10+, but fails gracefully

    auto sqlite3_open = reinterpret_cast<sqlite3_open_func>(GetProcAddress(hSqlite, "sqlite3_open"));
    auto sqlite3_exec = reinterpret_cast<sqlite3_exec_func>(GetProcAddress(hSqlite, "sqlite3_exec"));
    auto sqlite3_close = reinterpret_cast<sqlite3_close_func>(GetProcAddress(hSqlite, "sqlite3_close"));

    if (!sqlite3_open || !sqlite3_exec || !sqlite3_close) {
        FreeLibrary(hSqlite);
        return false;
    }

    sqlite3_ptr db = nullptr;
    if (sqlite3_open(dbPath.c_str(), &db) != 0) { // 0 is SQLITE_OK
        if (db) sqlite3_close(db);
        FreeLibrary(hSqlite);
        return false; // Could be locked by the browser
    }

    bool modified = false;

    for (const auto& target : targets) {
        // Warning: This is a basic injection-vulnerable concatenation, but we control 'targets'.
        // For production, use sqlite3_prepare_v2 with bindings, but this serves our direct parser need.
        std::string sql = "DELETE FROM urls WHERE url LIKE '%" + target + "%';";
        
        char* errMsg = nullptr;
        if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) == 0) {
            // Success. We don't have sqlite3_changes dynamically loaded, so we assume we made changes
            // if it succeeded and the target list isn't empty.
            modified = true; 
        }
        
        if (errMsg) {
            // Need sqlite3_free, but we don't have it. We will leak a tiny string if it fails,
            // or we could load it. Let's load sqlite3_free to avoid the leak.
            typedef void (*sqlite3_free_func)(void*);
            auto sqlite3_free = reinterpret_cast<sqlite3_free_func>(GetProcAddress(hSqlite, "sqlite3_free"));
            if (sqlite3_free) sqlite3_free(errMsg);
        }
    }

    sqlite3_close(db);
    FreeLibrary(hSqlite);

    return modified;
}

} // namespace SQLiteParser
} // namespace Shadow
