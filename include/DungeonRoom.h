#pragma once

#include <cstdint>

namespace dnf
{
using RoomId = std::uint32_t;

struct Position
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct RoomTemplate
{
    RoomId id = 0;

    // X축으로 이동할 수 있는 길이
    float width = 0.0f;

    // Y축으로 이동할 수 있는 깊이
    float depth = 0.0f;

    Position playerSpawn;
};

bool IsInsideRoom(const RoomTemplate& room, const Position& position);
} // namespace dnf
