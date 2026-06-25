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
- MSVC (Visual Studio) C++ Compiler
- CMake 3.20 or newer
