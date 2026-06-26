# Shadow

<p align="center">
  <img src="src/shadow.ico" width="128" alt="Shadow Icon" />
</p>

Shadow is a modular system privacy and cleanup utility for Windows. It is designed to safely clear local application caches, browser histories, and specific system execution logs.

## Features

- **Modular Cleanup:** Toggle specific cleanup modules on and off.
- **Dual-Mode Execution:**
  - **Debug Build:** Outputs a verbose, real-time scrolling console log detailing operations.
  - **Release Build:** Clean, minimalist interface.
- **Dry-Run Engine:** Simulate changes without modifying the system. Logs intended file deletions and registry modifications for review.
- **AI Safety Output:** Generates a text prompt containing targeted paths, designed for AI safety analysis.

## Build Instructions

Shadow uses CMake. To build the project:

1. Clone the repository:
   ```cmd
   git clone https://github.com/grasu14/Shadow.git
   cd Shadow
   ```

2. Configure with CMake:
   ```cmd
   cmake -B build
   ```

3. Build the project:
   ```cmd
   cmake --build build --config Release
   ```

The compiled binary will be placed in the `bin/Release` directory.

## Requirements

- Windows OS
- MSVC (Visual Studio) C++ Compiler (Requires "Desktop development with C++" workload)
- CMake 3.20 or newer

## Changelog

### v1.0.1 - Advanced Anti-Forensic Hardening & Build Pipeline Fixes

This massive update addresses critical forensic evasion flaws and stability issues in the original Shadow release. We undertook a complete code audit and hardening phase. 

#### 🛠️ What Problem We Had & How We Fixed It

**1. The Execution Cache (Prefetch) Vulnerability**
- **The Problem:** The old parser was fragile. It would crash with an Out-Of-Memory (OOM) error if a corrupted `.pf` file claimed a massive decompression size. It also used a naive case-sensitive string search that could miss mixed-case artifacts, and couldn't read modern Windows 10+ MAM compressed files.
- **The Fix:** We capped decompression sizes to 16MB to prevent OOM DOS. We normalized all scanning to case-insensitive, implemented proper `MultiByteToWideChar` UTF-16LE conversion, and added fallback support for `RtlDecompressBufferEx` to handle the latest Windows 10 compression algorithms.

**2. The File System Metadata (Timestomp) Fingerprinting**
- **The Problem:** The engine applied the exact same static timestamp to every wiped file. Forensic investigators could easily build a timeline query to find every single file that had this identical timestamp, instantly revealing what Shadow had touched.
- **The Fix:** We implemented a CSPRNG (`BCryptGenRandom`) jittering algorithm. Now, every file receives a randomized timestamp within a ±7 day window of the system baseline, allowing the wiped files to naturally blend into the surrounding NTFS environment.

**3. The Application Databases (SQLite) Recovery Risk**
- **The Problem:** The SQLite parser only deleted rows from the `urls` table, leaving tracking data in `visits`, `segments`, and other correlated tables. Worse, because it didn't manage the SQLite Write-Ahead Log (WAL) or free pages, forensic tools could easily recover the "deleted" history from the database's slack space. It was also vulnerable to SQL injection if a target name contained a quote.
- **The Fix:** We replaced raw string concatenation with parameterized SQL queries (`sqlite3_prepare_v2`). We expanded the sweep to hit all correlated tracking tables. Crucially, we implemented `PRAGMA wal_checkpoint(TRUNCATE)` and `VACUUM` to physically crush the database free pages after deletion, permanently destroying forensic recovery chances.

**4. The Hitlist Configuration Integrity**
- **The Problem:** The target configuration file (`targets.dat`) lacked integrity verification. A corrupted file could lead to wild and unpredictable wiping behavior. 
- **The Fix:** We implemented a CRC32 checksum system to mathematically verify the integrity of the hitlist before the engine commits to any filesystem operations.

**5. The Build Pipeline Struggles**
- **The Problem:** During the development of this update, we faced significant hurdles getting the automated background build tools to install the required Microsoft Visual Studio C++ build tools via the CLI (winget and vs_installer). The installer silently rejected headless workloads.
- **The Fix:** We documented the explicit requirement for manual workload installation (`Desktop development with C++`) inside the Visual Studio Installer GUI to ensure developers can successfully trigger the CMake compilation phase without background blocking.
