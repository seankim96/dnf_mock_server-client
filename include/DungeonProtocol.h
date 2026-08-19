#pragma once

#include "DungeonInstance.h"
#include "DungeonUdpTypes.h"

#include <cstdint>
#include <vector>

namespace dnf
{
constexpr std::uint16_t DUNGEON_PROTOCOL_VERSION = 1;

struct PlayerMovementMessage
{
    DungeonId dungeonId = 0;
    std::uint32_t sequence = 0;
    float moveX = 0.0f;
    float moveY = 0.0f;
    bool jump = false;
};

struct PlayerAttackMessage
{
    DungeonId dungeonId = 0;
    std::uint32_t sequence = 0;
    std::uint32_t skillId = 0;
    float directionX = 0.0f;
    float directionY = 0.0f;
};

struct UdpHelloMessage
{
    DungeonId dungeonId = 0;
    SessionId sessionId = 0;
    DungeonUdpToken token = 0;
};

struct UdpHeartbeatMessage
{
    DungeonId dungeonId = 0;
    SessionId sessionId = 0;
};

std::vector<std::uint8_t> EncodePlayerMovement(
    const PlayerMovementMessage& movement);

bool DecodePlayerMovement(
    const std::vector<std::uint8_t>& bytes,
    PlayerMovementMessage& output);

std::vector<std::uint8_t> EncodePlayerAttack(
    const PlayerAttackMessage& attack);

bool DecodePlayerAttack(
    const std::vector<std::uint8_t>& bytes,
    PlayerAttackMessage& output);

std::vector<std::uint8_t> EncodeUdpHello(
    const UdpHelloMessage& hello);

bool DecodeUdpHello(
    const std::vector<std::uint8_t>& bytes,
    UdpHelloMessage& output);

std::vector<std::uint8_t> EncodeUdpHeartbeat(
    const UdpHeartbeatMessage& heartbeat);

bool DecodeUdpHeartbeat(
    const std::vector<std::uint8_t>& bytes,
    UdpHeartbeatMessage& output);

std::vector<std::uint8_t> EncodeDungeonSnapshot(
    const DungeonInstance& dungeon,
    std::uint32_t serverTick);
} // namespace dnf
