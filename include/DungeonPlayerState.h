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
    Dead,
    WrongRoom,
    OutsideRoom,
    BlockedByObstacle
};

enum class BeginSkillResult
{
    Success,
    Dead,
    InvalidSkill,
    NotEnoughMana,
    OnCooldown,
    Busy
};

enum class SkillActionPhase
{
    Idle,
    Startup,
    Active,
    Recovery
};

struct SkillActionSnapshot
{
    SkillId skillId = 0;
    SkillActionPhase phase = SkillActionPhase::Idle;
    std::uint32_t remainingTicks = 0;
};

struct DungeonPlayerSnapshot
{
    RoomId roomId = 0;
    Position position;
    std::uint32_t currentHp = 0;
    std::uint32_t maxHp = 0;
    bool alive = false;
    std::uint32_t currentMp = 0;
    std::uint32_t maxMp = 0;
    SkillActionSnapshot skillAction;
};

class DungeonPlayerState
{
public:
    DungeonPlayerState(
        SessionId sessionId,
        RoomId roomId,
        Position position,
        std::uint32_t maxMp = 100,
        std::uint32_t maxHp = 100);

    SessionId Session() const;
    RoomId CurrentRoom() const;
    Position CurrentPosition() const;
    DungeonPlayerSnapshot Snapshot() const;
    std::uint32_t CurrentHp() const;
    bool IsAlive() const;
    bool ApplyDamage(std::uint32_t damage);
    std::uint32_t CurrentMp() const;
    std::uint32_t RemainingCooldown(SkillId skillId) const;
    SkillActionSnapshot CurrentSkillAction() const;

    BeginSkillResult BeginSkill(
        SkillId skillId,
        std::uint32_t manaCost,
        std::uint32_t cooldownTicks,
        std::uint32_t startupTicks,
        std::uint32_t activeTicks,
        std::uint32_t recoveryTicks);
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
    std::uint32_t currentHp_;
    std::uint32_t maxHp_;
    std::uint32_t currentMp_;
    std::uint32_t maxMp_;
    std::unordered_map<SkillId, std::uint32_t> cooldowns_;

    SkillId actionSkillId_ = 0;
    SkillActionPhase actionPhase_ = SkillActionPhase::Idle;
    std::uint32_t actionRemainingTicks_ = 0;
    std::uint32_t actionActiveTicks_ = 0;
    std::uint32_t actionRecoveryTicks_ = 0;
};
} // namespace dnf
