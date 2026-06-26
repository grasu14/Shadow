#include "sqlite_parser.h"
#include "logger.h"
#include <Windows.h>

namespace Shadow {
namespace SQLiteParser {

// ─── SQLite function typedefs (dynamically loaded from winsqlite3.dll) ──────

typedef void*  sqlite3_ptr;
typedef void*  sqlite3_stmt_ptr;

typedef int   (*sqlite3_open_func)(const char* filename, sqlite3_ptr* ppDb);
typedef int   (*sqlite3_exec_func)(sqlite3_ptr, const char* sql,
                                    int (*callback)(void*, int, char**, char**),
                                    void*, char** errmsg);
typedef int   (*sqlite3_close_func)(sqlite3_ptr);
typedef void  (*sqlite3_free_func)(void*);
typedef int   (*sqlite3_changes_func)(sqlite3_ptr);
typedef int   (*sqlite3_prepare_v2_func)(sqlite3_ptr, const char* sql, int nByte,
                                          sqlite3_stmt_ptr* ppStmt, const char** pzTail);
typedef int   (*sqlite3_bind_text_func)(sqlite3_stmt_ptr, int, const char*, int,
                                         void(*)(void*));
typedef int   (*sqlite3_step_func)(sqlite3_stmt_ptr);
typedef int   (*sqlite3_finalize_func)(sqlite3_stmt_ptr);
typedef int   (*sqlite3_column_int64_func)(sqlite3_stmt_ptr, int);
typedef const char* (*sqlite3_errmsg_func)(sqlite3_ptr);

// SQLite constants
constexpr int SQLITE_OK       = 0;
constexpr int SQLITE_ROW      = 100;
constexpr int SQLITE_DONE     = 101;
constexpr int SQLITE_BUSY     = 5;
constexpr int SQLITE_LOCKED   = 6;
#define SQLITE_TRANSIENT reinterpret_cast<void(*)(void*)>(-1)

// ─── Lazy-loaded function table (never freed — system DLL) ──────────────────

struct SqliteFuncs {
    HMODULE                   hLib           = nullptr;
    sqlite3_open_func         open           = nullptr;
    sqlite3_exec_func         exec           = nullptr;
    sqlite3_close_func        close          = nullptr;
    sqlite3_free_func         free           = nullptr;
    sqlite3_changes_func      changes        = nullptr;
    sqlite3_prepare_v2_func   prepare_v2     = nullptr;
    sqlite3_bind_text_func    bind_text      = nullptr;
    sqlite3_step_func         step           = nullptr;
    sqlite3_finalize_func     finalize       = nullptr;
    sqlite3_column_int64_func column_int64   = nullptr;
    sqlite3_errmsg_func       errmsg         = nullptr;
    bool                      valid          = false;
    bool                      resolved       = false;
};

static SqliteFuncs& getSqlite() {
    static SqliteFuncs f;
    if (!f.resolved) {
        f.hLib = LoadLibraryA("winsqlite3.dll");
        if (f.hLib) {
            f.open         = reinterpret_cast<sqlite3_open_func>(GetProcAddress(f.hLib, "sqlite3_open"));
            f.exec         = reinterpret_cast<sqlite3_exec_func>(GetProcAddress(f.hLib, "sqlite3_exec"));
            f.close        = reinterpret_cast<sqlite3_close_func>(GetProcAddress(f.hLib, "sqlite3_close"));
            f.free         = reinterpret_cast<sqlite3_free_func>(GetProcAddress(f.hLib, "sqlite3_free"));
            f.changes      = reinterpret_cast<sqlite3_changes_func>(GetProcAddress(f.hLib, "sqlite3_changes"));
            f.prepare_v2   = reinterpret_cast<sqlite3_prepare_v2_func>(GetProcAddress(f.hLib, "sqlite3_prepare_v2"));
            f.bind_text    = reinterpret_cast<sqlite3_bind_text_func>(GetProcAddress(f.hLib, "sqlite3_bind_text"));
            f.step         = reinterpret_cast<sqlite3_step_func>(GetProcAddress(f.hLib, "sqlite3_step"));
            f.finalize     = reinterpret_cast<sqlite3_finalize_func>(GetProcAddress(f.hLib, "sqlite3_finalize"));
            f.column_int64 = reinterpret_cast<sqlite3_column_int64_func>(GetProcAddress(f.hLib, "sqlite3_column_int64"));
            f.errmsg       = reinterpret_cast<sqlite3_errmsg_func>(GetProcAddress(f.hLib, "sqlite3_errmsg"));

            f.valid = f.open && f.exec && f.close && f.free && f.changes &&
                      f.prepare_v2 && f.bind_text && f.step && f.finalize;
        }
        f.resolved = true;
    }
    return f;
}

// ─── Internal: execute a parameterized DELETE with LIKE binding ─────────────

static int execParameterizedDelete(const SqliteFuncs& sql, sqlite3_ptr db,
                                    const char* statement, const std::string& likePattern) {
    sqlite3_stmt_ptr stmt = nullptr;
    if (sql.prepare_v2(db, statement, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }

    sql.bind_text(stmt, 1, likePattern.c_str(),
                  static_cast<int>(likePattern.size()), SQLITE_TRANSIENT);

    int result = sql.step(stmt);
    sql.finalize(stmt);

    if (result == SQLITE_DONE) {
        return sql.changes(db);
    }
    return 0;
}

// ─── Internal: forensic compaction ──────────────────────────────────────────

static bool doCompact(const SqliteFuncs& sql, sqlite3_ptr db) {
    char* errMsg = nullptr;
    bool success = true;

    // Flush WAL journal to main database file, then truncate the WAL
    if (sql.exec(db, "PRAGMA wal_checkpoint(TRUNCATE);", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) { sql.free(errMsg); errMsg = nullptr; }
        success = false;
    }

    // VACUUM rewrites the entire database, eliminating free pages that
    // contain recoverable deleted data
    if (sql.exec(db, "VACUUM;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
        if (errMsg) { sql.free(errMsg); errMsg = nullptr; }
        success = false;
    }

    return success;
}

// ─── removeTargetsFromHistory (Chromium) ────────────────────────────────────

bool removeTargetsFromHistory(const std::string& dbPath,
                               const std::vector<std::string>& targets,
                               bool isDryRun) {
    if (targets.empty()) return false;

    auto& sql = getSqlite();
    if (!sql.valid) return false;

    Logger& log = Logger::instance();

    sqlite3_ptr db = nullptr;
    int rc = sql.open(dbPath.c_str(), &db);

    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
        log.log(LogLevel::WARNING,
            "SQLite: '" + dbPath + "' is locked — close the browser and retry.");
        if (db) sql.close(db);
        return false;
    }

    if (rc != SQLITE_OK) {
        if (db) sql.close(db);
        return false;
    }

    // Set a busy timeout to handle transient locks
    sql.exec(db, "PRAGMA busy_timeout = 2000;", nullptr, nullptr, nullptr);

    int totalChanges = 0;

    for (const auto& target : targets) {
        std::string likePattern = "%" + target + "%";

        if (isDryRun) {
            // Count matching rows without deleting
            sqlite3_stmt_ptr stmt = nullptr;
            const char* countSql = "SELECT COUNT(*) FROM urls WHERE url LIKE ?1;";
            if (sql.prepare_v2(db, countSql, -1, &stmt, nullptr) == SQLITE_OK) {
                sql.bind_text(stmt, 1, likePattern.c_str(),
                              static_cast<int>(likePattern.size()), SQLITE_TRANSIENT);
                if (sql.step(stmt) == SQLITE_ROW && sql.column_int64) {
                    int64_t count = sql.column_int64(stmt, 0);
                    if (count > 0) {
                        log.log(LogLevel::DEBUG_DETAIL,
                            "SQLite dry-run: would delete " + std::to_string(count) +
                            " rows matching '" + target + "' from " + dbPath);
                        totalChanges += static_cast<int>(count);
                    }
                }
                sql.finalize(stmt);
            }
            continue;
        }

        // 1. Delete from visits (FK reference to urls.id)
        execParameterizedDelete(sql, db,
            "DELETE FROM visits WHERE url IN (SELECT id FROM urls WHERE url LIKE ?1);",
            likePattern);

        // 2. Delete from keyword_search_terms (FK reference to urls.id)
        execParameterizedDelete(sql, db,
            "DELETE FROM keyword_search_terms WHERE url_id IN (SELECT id FROM urls WHERE url LIKE ?1);",
            likePattern);

        // 3. Delete from segment_usage via segments
        execParameterizedDelete(sql, db,
            "DELETE FROM segment_usage WHERE segment_id IN "
            "(SELECT id FROM segments WHERE url_id IN (SELECT id FROM urls WHERE url LIKE ?1));",
            likePattern);

        // 4. Delete from segments
        execParameterizedDelete(sql, db,
            "DELETE FROM segments WHERE url_id IN (SELECT id FROM urls WHERE url LIKE ?1);",
            likePattern);

        // 5. Delete from urls (primary target)
        int changed = execParameterizedDelete(sql, db,
            "DELETE FROM urls WHERE url LIKE ?1;",
            likePattern);

        totalChanges += changed;
    }

    // Forensic compaction: eliminate recoverable data
    if (!isDryRun && totalChanges > 0) {
        doCompact(sql, db);
    }

    sql.close(db);
    return totalChanges > 0;
}

// ─── removeTargetsFromFirefoxHistory ────────────────────────────────────────

bool removeTargetsFromFirefoxHistory(const std::string& dbPath,
                                      const std::vector<std::string>& targets,
                                      bool isDryRun) {
    if (targets.empty()) return false;

    auto& sql = getSqlite();
    if (!sql.valid) return false;

    Logger& log = Logger::instance();

    sqlite3_ptr db = nullptr;
    int rc = sql.open(dbPath.c_str(), &db);

    if (rc == SQLITE_BUSY || rc == SQLITE_LOCKED) {
        log.log(LogLevel::WARNING,
            "SQLite: '" + dbPath + "' is locked — close Firefox and retry.");
        if (db) sql.close(db);
        return false;
    }

    if (rc != SQLITE_OK) {
        if (db) sql.close(db);
        return false;
    }

    sql.exec(db, "PRAGMA busy_timeout = 2000;", nullptr, nullptr, nullptr);

    int totalChanges = 0;

    for (const auto& target : targets) {
        std::string likePattern = "%" + target + "%";

        if (isDryRun) {
            sqlite3_stmt_ptr stmt = nullptr;
            const char* countSql = "SELECT COUNT(*) FROM moz_places WHERE url LIKE ?1;";
            if (sql.prepare_v2(db, countSql, -1, &stmt, nullptr) == SQLITE_OK) {
                sql.bind_text(stmt, 1, likePattern.c_str(),
                              static_cast<int>(likePattern.size()), SQLITE_TRANSIENT);
                if (sql.step(stmt) == SQLITE_ROW && sql.column_int64) {
                    int64_t count = sql.column_int64(stmt, 0);
                    if (count > 0) {
                        log.log(LogLevel::DEBUG_DETAIL,
                            "SQLite dry-run: would delete " + std::to_string(count) +
                            " Firefox rows matching '" + target + "'");
                        totalChanges += static_cast<int>(count);
                    }
                }
                sql.finalize(stmt);
            }
            continue;
        }

        // 1. Delete visit history
        execParameterizedDelete(sql, db,
            "DELETE FROM moz_historyvisits WHERE place_id IN "
            "(SELECT id FROM moz_places WHERE url LIKE ?1);",
            likePattern);

        // 2. Delete input history (autocomplete data)
        execParameterizedDelete(sql, db,
            "DELETE FROM moz_inputhistory WHERE place_id IN "
            "(SELECT id FROM moz_places WHERE url LIKE ?1);",
            likePattern);

        // 3. Delete from places (primary target)
        int changed = execParameterizedDelete(sql, db,
            "DELETE FROM moz_places WHERE url LIKE ?1;",
            likePattern);

        totalChanges += changed;
    }

    if (!isDryRun && totalChanges > 0) {
        doCompact(sql, db);
    }

    sql.close(db);
    return totalChanges > 0;
}

// ─── compactDatabase (standalone) ───────────────────────────────────────────

bool compactDatabase(const std::string& dbPath) {
    auto& sql = getSqlite();
    if (!sql.valid) return false;

    sqlite3_ptr db = nullptr;
    if (sql.open(dbPath.c_str(), &db) != SQLITE_OK) {
        if (db) sql.close(db);
        return false;
    }

    bool result = doCompact(sql, db);
    sql.close(db);
    return result;
}

} // namespace SQLiteParser
} // namespace Shadow
