#pragma once
#include "engine.h"
#include <string>

namespace Shadow {

/// Interactive CLI menu for toggling modules, switching dry-run mode,
/// and launching execution.
class Menu {
public:
    explicit Menu(Engine& engine);
    void run();

private:
    void drawHeader();
    void drawModules();
    void drawOptions();
    void drawPrompt();
    void handleInput(const std::string& input);
    void toggleModule(int id);
    void selectAll(bool state);

    Engine& m_engine;
    bool    m_running = true;
};

} // namespace Shadow
