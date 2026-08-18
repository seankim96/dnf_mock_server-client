#pragma once

#include "DungeonInstance.h"
#include "DungeonUdpTypes.h"

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

struct UdpHelloMessage
{
    DungeonId dungeonId = 0;
    SessionId sessionId = 0;
    DungeonUdpToken token = 0;
};

std::vector<std::uint8_t> EncodePlayerInput(
    const PlayerInputMessage& input);

bool DecodePlayerInput(
    const std::vector<std::uint8_t>& bytes,
    PlayerInputMessage& output);

std::vector<std::uint8_t> EncodeUdpHello(
    const UdpHelloMessage& hello);

bool DecodeUdpHello(
    const std::vector<std::uint8_t>& bytes,
    UdpHelloMessage& output);

std::vector<std::uint8_t> EncodeDungeonSnapshot(
    const DungeonInstance& dungeon,
    std::uint32_t serverTick);
} // namespace dnf
