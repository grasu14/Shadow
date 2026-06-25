#pragma once

namespace Shadow {
class Engine;

namespace Modules {
    /// Registers all parsing-based cleaning modules into the Engine.
    void registerAll(Engine& engine);
} // namespace Modules
} // namespace Shadow
