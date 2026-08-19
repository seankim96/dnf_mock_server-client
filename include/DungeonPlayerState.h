#pragma once

#include "DungeonRoom.h"
#include "RoomState.h"
#include "SessionId.h"
#include "SkillCatalog.h"

#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace dnf
{
enum class MovePlayerResult
{
    Success,
    WrongRoom,
    OutsideRoom,
    BlockedByObstacle
};

enum class BeginSkillResult
{
    Success,
    InvalidSkill,
    NotEnoughMana,
    OnCooldown
};

struct DungeonPlayerSnapshot
{
    RoomId roomId = 0;
    Position position;
    std::uint32_t currentMp = 0;
    std::uint32_t maxMp = 0;
};

class DungeonPlayerState
{
public:
    DungeonPlayerState(
        SessionId sessionId,
        RoomId roomId,
        Position position,
        std::uint32_t maxMp = 100);

    SessionId Session() const;
    RoomId CurrentRoom() const;
    Position CurrentPosition() const;
    DungeonPlayerSnapshot Snapshot() const;
    std::uint32_t CurrentMp() const;
    std::uint32_t RemainingCooldown(SkillId skillId) const;

    BeginSkillResult BeginSkill(
        SkillId skillId,
        std::uint32_t manaCost,
        std::uint32_t cooldownTicks);
    void AdvanceCombatTick();

    MovePlayerResult MoveTo(
        const RoomState& room,
        const Position& nextPosition);

    MovePlayerResult EnterRoom(
        const RoomState& room,
        const Position& spawnPosition);

private:
    SessionId sessionId_;

    mutable std::mutex mutex_;
    RoomId roomId_;
    Position position_;
    std::uint32_t currentMp_;
    std::uint32_t maxMp_;
    std::unordered_map<SkillId, std::uint32_t> cooldowns_;
};
} // namespace dnf
