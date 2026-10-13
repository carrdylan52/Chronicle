// Dark Cloud as an Ixion module: a shared library the host loads beside Dark Chronicle's. Only
// ixion_get_api is exported (ixion/include/ixion_api.h); everything else stays inside the module.
// The host gives a fiber to run() and switches between the games at frame ends (EmbedCheckpoint).

#include "ixion_api.h"

#include <cstdint>

#include "character.hpp"
#include "dun/gameloop.hpp"
#include "editloop.hpp"
#include "gameloop.hpp"
#include "main.hpp"
#include "mainselect.hpp"
#include "platform/audio.hpp"
#include "platform/clock.hpp"
#include "platform/embed.hpp"
#include "platform/mouse.hpp"
#include "platform/window.hpp"
#include "userstatus.hpp"

// The union is only named here: the hooks pass SDL_Event pointers through and never look inside.
union SDL_Event;

// main.cpp's run, which the standalone executable's main calls too.
int RunDarkCloud(int argc, const char **argv);

namespace {

constexpr std::uint32_t kGameId = IXION_GAME_DARK_CLOUD;

// The host's services, copied at bind: the host struct outlives the module's use of it.
IxionHost g_host = {};
bool      g_bound = false;

bool PollEvent(void *, SDL_Event *event) {
    return g_host.poll_event != nullptr && g_host.poll_event(g_host.user, kGameId, event) != 0;
}

bool Checkpoint(void *, int reason) {
    return g_host.checkpoint != nullptr &&
           g_host.checkpoint(g_host.user, kGameId, static_cast<std::uint32_t>(reason)) != 0;
}

int Bind(const IxionHost *host) {
    if (host == nullptr || host->size < sizeof(IxionHost) || host->abi_version != IXION_ABI_VERSION) {
        return 1;
    }
    g_host = *host;
    g_bound = true;
    return 0;
}

int Run(int argc, char **argv) {
    if (!g_bound) {
        return 1;
    }
    EmbedHooks hooks;
    hooks.poll_event = PollEvent;
    hooks.checkpoint = Checkpoint;
    EmbedEnable(hooks);
    return RunDarkCloud(argc, const_cast<const char **>(argv));
}

void Activate(int active) {
    AudioOutputPause(active == 0);
    // Capture is the game's while it is the visible one; giving it up on deactivation keeps the
    // cursor free for the other game's window.
    if (active != 0) {
        MouseStart();
    } else {
        MouseStop();
    }
}

void *Window() {
    return WindowHandle();
}

// The receipt fields the host logs and compares across a park. Guessed mappings are marked.
int Inspect(IxionReceipt *out) {
    if (out == nullptr || out->size < sizeof(IxionReceipt)) {
        return 1;
    }
    // old_main_mode is the mode the current frame ran in; RunGame sets it before each frame.
    const int mode = old_main_mode;
    out->game_id = kGameId;
    out->logic_frames = static_cast<std::uint64_t>(GameFrameCount());
    out->clock_ticks = static_cast<std::uint64_t>(ClockTickCount());
    out->mode = mode;
    out->map = MapNo;
    out->floor = -1;
    out->character = -1;
    out->player_pos[0] = out->player_pos[1] = out->player_pos[2] = 0.0f;
    out->place[0] = '\0';
    // Guess: UserStatus->cur_chara is the index of the party member the pad controls.
    if (UserStatus != nullptr) {
        out->character = UserStatus->cur_chara;
        // cur_floor is the dungeon floor the party stands on.
        if (mode == GAME_MODE_DUNGEON) {
            out->floor = UserStatus->cur_floor;
        }
    }
    // Guess: MainChara is the controlled character in towns and dungeons; pos is its world position.
    if (mode == GAME_MODE_EDIT || mode == GAME_MODE_DUNGEON) {
        for (int axis = 0; axis < 3; axis++) {
            out->player_pos[axis] = MainChara.pos[axis];
        }
    }
    return 0;
}

const IxionApi g_api = {
    .size = sizeof(IxionApi),
    .abi_version = IXION_ABI_VERSION,
    .game_id = kGameId,
    .name = "Dark Cloud",
    .bind = Bind,
    .run = Run,
    .activate = Activate,
    .window = Window,
    .inspect = Inspect,
};

} // namespace

IXION_EXPORT const IxionApi *ixion_get_api(uint32_t abi_version) {
    return abi_version == IXION_ABI_VERSION ? &g_api : nullptr;
}
