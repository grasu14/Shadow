#include "engine.h"
#include "menu.h"
#include "utils.h"
#include "modules.h"
#include "config.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc == 4 && std::string(argv[1]) == "--encrypt") {
        std::string inFile = argv[2];
        std::string outFile = argv[3];
        if (Shadow::Config::instance().encryptToFile(inFile, outFile)) {
            std::cout << "Successfully encrypted " << inFile << " to " << outFile << "\n";
            return 0;
        } else {
            std::cerr << "Failed to encrypt file.\n";
            return 1;
        }
    }

    Shadow::Utils::setupConsole();

    // ── Load Configuration ──────────────────────────────────────────────────
    if (!Shadow::Config::instance().loadEncrypted("targets.dat")) {
        std::cerr << "\033[33m\033[1m\n"
                  << "  [!] WARNING: Could not load or parse 'targets.dat'.\n"
                  << "  [!] Shadow is running without target specifications.\n"
                  << "\033[0m\n"
                  << "  Press Enter to continue anyway...";
        std::cin.get();
    }

    // ── Administrator check ─────────────────────────────────────────────────
    if (!Shadow::Utils::isRunningAsAdmin()) {
        std::cerr << "\033[31m\033[1m\n"
                  << "  [!] Shadow must be run as Administrator.\n"
                  << "  [!] Right-click the executable and select "
                     "'Run as administrator'.\n"
                  << "\033[0m\n"
                  << "  Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    // ── Engine setup ────────────────────────────────────────────────────────
    Shadow::Engine engine;

    // Register all parsing-based cleaning modules.
    Shadow::Modules::registerAll(engine);

    // ── Launch CLI ──────────────────────────────────────────────────────────
    Shadow::Menu menu(engine);
    menu.run();

    return 0;
}
