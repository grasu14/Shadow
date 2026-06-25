#include "menu.h"
#include "utils.h"

#include <algorithm>
#include <iostream>
#include <string>

namespace Shadow {

// ─── ANSI helpers (menu only) ───────────────────────────────────────────────
namespace MC {
    constexpr const char* RST  = "\033[0m";
    constexpr const char* BOLD = "\033[1m";
    constexpr const char* DIM  = "\033[2m";
    constexpr const char* RED  = "\033[31m";
    constexpr const char* GRN  = "\033[32m";
    constexpr const char* YEL  = "\033[33m";
    constexpr const char* CYN  = "\033[36m";
    constexpr const char* WHT  = "\033[97m";
}

Menu::Menu(Engine& engine) : m_engine(engine) {}

// ─── Main loop ──────────────────────────────────────────────────────────────

void Menu::run() {
    while (m_running) {
        Utils::clearScreen();
        drawHeader();
        drawModules();
        drawOptions();
        drawPrompt();

        std::string input;
        std::getline(std::cin, input);

        if (std::cin.eof() || std::cin.fail()) break;

        handleInput(input);
    }
}

// ─── Drawing ────────────────────────────────────────────────────────────────

void Menu::drawHeader() {
    std::cout << MC::CYN << MC::BOLD << "\n"
        << "  =============================================\n"
        << "            S H A D O W   v1.0                \n"
        << "       System Privacy & Cleanup Utility        \n"
        << "  =============================================\n"
        << MC::RST;

    // Mode badge
    std::cout << "  Mode: ";
    if (m_engine.IS_DRY_RUN)
        std::cout << MC::YEL << MC::BOLD << "[DRY-RUN]" << MC::RST;
    else
        std::cout << MC::RED << MC::BOLD << "[LIVE]"    << MC::RST;

    // Build badge
#ifdef SHADOW_DEBUG
    std::cout << "              Build: "
              << MC::YEL << "DEBUG" << MC::RST;
#else
    std::cout << "              Build: "
              << MC::GRN << "RELEASE" << MC::RST;
#endif
    std::cout << "\n"
              << MC::DIM
              << "  ---------------------------------------------"
              << MC::RST << "\n";
}

void Menu::drawModules() {
    std::cout << "\n"
              << MC::WHT << MC::BOLD
              << "  Cleanup Modules:" << MC::RST << "\n\n";

    const auto& modules = m_engine.getModules();

    if (modules.empty()) {
        std::cout << MC::DIM
                  << "    (no modules registered)"
                  << MC::RST << "\n";
        return;
    }

    for (const auto& mod : modules) {
        std::cout << "   "
                  << MC::DIM << "[" << mod.id << "]" << MC::RST << " ";

        if (mod.enabled) {
            std::cout << MC::GRN << "[X]" << MC::RST << " "
                      << MC::WHT << mod.name << MC::RST;
        } else {
            std::cout << MC::DIM << "[ ] " << mod.name << MC::RST;
        }
        std::cout << "\n";
    }

    std::cout << "\n";
}

void Menu::drawOptions() {
    std::cout << MC::DIM
              << "  ---------------------------------------------"
              << MC::RST << "\n";

    std::cout << "   " << MC::CYN << "[D]" << MC::RST << " Toggle Dry-Run"
              << "       " << MC::CYN << "[A]" << MC::RST << " Select All\n";

    std::cout << "   " << MC::CYN << "[N]" << MC::RST << " Deselect All"
              << "        " << MC::GRN << MC::BOLD << "[R]" << MC::RST
              << " Run\n";

    std::cout << "   " << MC::RED << "[Q]" << MC::RST << " Quit\n";

    std::cout << MC::DIM
              << "  ---------------------------------------------"
              << MC::RST << "\n";
}

void Menu::drawPrompt() {
    std::cout << "\n  " << MC::WHT << "> " << MC::RST;
}

// ─── Input handling ─────────────────────────────────────────────────────────

void Menu::handleInput(const std::string& input) {
    if (input.empty()) return;

    std::string cmd = input;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // ── Quit ────────────────────────────────────────────────────────────────
    if (cmd == "q" || cmd == "quit") {
        m_running = false;
        return;
    }

    // ── Toggle dry-run ──────────────────────────────────────────────────────
    if (cmd == "d") {
        m_engine.IS_DRY_RUN = !m_engine.IS_DRY_RUN;
        return;
    }

    // ── Select / deselect all ───────────────────────────────────────────────
    if (cmd == "a") { selectAll(true);  return; }
    if (cmd == "n") { selectAll(false); return; }

    // ── Run ─────────────────────────────────────────────────────────────────
    if (cmd == "r" || cmd == "run") {
        // Guard: nothing enabled
        bool anyEnabled = false;
        for (const auto& m : m_engine.getModules())
            if (m.enabled) { anyEnabled = true; break; }

        if (!anyEnabled) {
            Utils::clearScreen();
            std::cout << MC::YEL
                      << "\n  [!] No modules selected. "
                         "Enable at least one module first.\n"
                      << MC::RST
                      << "\n  Press Enter to continue...";
            std::cin.get();
            return;
        }

        // Safety gate for live mode
        if (!m_engine.IS_DRY_RUN) {
            Utils::clearScreen();
            std::cout << MC::RED << MC::BOLD
                << "\n  ===========================================\n"
                << "   WARNING: LIVE MODE - Changes are permanent\n"
                << "  ===========================================\n"
                << MC::RST
                << "\n  Type 'confirm' to proceed, or Enter to cancel: ";

            std::string confirmation;
            std::getline(std::cin, confirmation);
            if (confirmation != "confirm") return;
        }

        Utils::clearScreen();
        std::cout << MC::CYN
                  << "\n  Starting execution...\n"
                  << MC::RST << "\n";

        m_engine.execute();

        std::cout << MC::RED << MC::BOLD
                  << "\n  ===========================================\n"
                  << "   EXECUTION COMPLETE\n"
                  << "   Type 'shadow' to confirm and exit.\n"
                  << "  ===========================================\n"
                  << MC::RST
                  << "\n  > ";

        std::string exitCmd;
        while (true) {
            std::getline(std::cin, exitCmd);
            if (exitCmd == "shadow") {
                m_running = false;
                break;
            }
        }
        return;
    }

    // ── Numeric toggle ──────────────────────────────────────────────────────
    try {
        int num = std::stoi(cmd);
        toggleModule(num);
    } catch (...) {
        // Unrecognised input — silently redraw.
    }
}

void Menu::toggleModule(int id) {
    for (auto& mod : m_engine.getModules()) {
        if (mod.id == id) {
            mod.enabled = !mod.enabled;
            return;
        }
    }
}

void Menu::selectAll(bool state) {
    for (auto& mod : m_engine.getModules())
        mod.enabled = state;
}

} // namespace Shadow
