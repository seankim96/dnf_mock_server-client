#pragma once

#include "DungeonManager.h"
#include "DungeonUdpManager.h"

#include <chrono>
#include <optional>

namespace dnf
{
constexpr auto DEFAULT_DUNGEON_RECONNECT_GRACE =
    std::chrono::seconds(30);

struct DungeonSessionRegistration
{
    RegisterDungeonSessionStatus status =
        RegisterDungeonSessionStatus::InvalidIdentity;
    DungeonId dungeonId = 0;
    SessionId replacedSessionId = 0;
    std::optional<DungeonUdpToken> freshUdpToken;
};

class DungeonLifecycleService
{
public:
    DungeonLifecycleService(
        DungeonManager& dungeonManager,
        DungeonUdpManager& udpManager,
        std::chrono::milliseconds reconnectGrace =
            DEFAULT_DUNGEON_RECONNECT_GRACE);

    DungeonSessionRegistration RegisterPlayerSession(
        SessionId sessionId,
        PlayerId playerId);
    bool DisconnectPlayerSession(SessionId sessionId);
    DungeonAbandonmentSweep SweepAbandonedParticipants(
        std::chrono::steady_clock::time_point now,
        std::chrono::milliseconds reconnectGrace,
        std::chrono::milliseconds maxDungeonLifetime);
    bool CancelWaitingDungeon(DungeonId dungeonId);
    bool FinishDungeon(DungeonId dungeonId);

private:
    DungeonManager& dungeonManager_;
    DungeonUdpManager& udpManager_;
    std::chrono::milliseconds reconnectGrace_;
};
} // namespace dnf
