#pragma once

#include <filesystem>
#include <memory>

#include "gfx/gfx.hpp"
#include "platform/window.hpp"

namespace dcvr {
// Only dcvr_game links this host. Ordinary darkcloud has no OpenXR dependency.
class GameBridge {
public:
    GameBridge(std::filesystem::path synthetic, int seconds, float units_per_metre);
    ~GameBridge();
    void Configure(WindowConfig &window, gfx::RendererConfig &renderer);
    void Start();
    void Pump();
    void Stop();
    bool Succeeded() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace dcvr
