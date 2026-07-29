/// @file player.cpp
/// @brief IPlayer 工厂方法.

#include "api/player.h"
#include "control/player_controller/player_controller.h"

namespace player {

std::unique_ptr<IPlayer> IPlayer::create() {
  return std::make_unique<PlayerController>();
}

std::unique_ptr<IPlayer> IPlayer::create(const PlayerConfig& config) {
  return std::make_unique<PlayerController>(config);
}

} // namespace player
