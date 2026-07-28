#include "player.h"

namespace player {

// ══════════════════════════════════════════════════════════════════════════════
// Factory — IPlayer::create()
// ══════════════════════════════════════════════════════════════════════════════

std::unique_ptr<IPlayer> IPlayer::create()
{
    // TODO: Return a concrete PlayerController instance
    //
    //   #include "control/player_controller/player_controller.h"
    //   return std::make_unique<PlayerController>();
    //
    return nullptr;
}

std::unique_ptr<IPlayer> IPlayer::create(const PlayerConfig& config)
{
    // TODO: Return a concrete PlayerController instance with the given config
    //
    //   #include "control/player_controller/player_controller.h"
    //   return std::make_unique<PlayerController>(config);
    //
    return nullptr;
}

} // namespace player
