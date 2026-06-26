#pragma once
#include <string>
#include <vector>

namespace Shadow {
namespace SQLiteParser {

    /// Removes all matching URL records (and associated visits, search terms,
    /// segments) from a Chromium-based browser History database.
    /// Uses parameterized queries to avoid SQL injection.
    /// Performs WAL checkpoint + VACUUM to eliminate forensic recovery from
    /// free pages and WAL journal files.
    /// Returns true if successful and modifications were made.
    bool removeTargetsFromHistory(const std::string& dbPath,
                                  const std::vector<std::string>& targets,
                                  bool isDryRun = false);

    /// Removes matching records from a Firefox places.sqlite database
    /// (moz_places, moz_historyvisits, moz_inputhistory tables).
    /// Returns true if successful and modifications were made.
    bool removeTargetsFromFirefoxHistory(const std::string& dbPath,
                                         const std::vector<std::string>& targets,
                                         bool isDryRun = false);

    /// Performs WAL checkpoint (TRUNCATE) and VACUUM on a database to
    /// eliminate recoverable deleted data from free pages and journal files.
    bool compactDatabase(const std::string& dbPath);

} // namespace SQLiteParser
} // namespace Shadow
