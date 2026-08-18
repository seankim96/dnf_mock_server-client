#pragma once

#include "DungeonInstance.h"

#include <cstdint>
#include <vector>

namespace dnf
{
constexpr std::uint16_t DUNGEON_PROTOCOL_VERSION = 1;

struct PlayerInputMessage
{
    DungeonId dungeonId = 0;
    std::uint32_t sequence = 0;
    float moveX = 0.0f;
    float moveY = 0.0f;
    bool jump = false;
};

std::vector<std::uint8_t> EncodePlayerInput(
    const PlayerInputMessage& input);

bool DecodePlayerInput(
    const std::vector<std::uint8_t>& bytes,
    PlayerInputMessage& output);
} // namespace dnf
