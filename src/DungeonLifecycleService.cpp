#include "DungeonLifecycleService.h"

#include <stdexcept>

namespace dnf
{
DungeonLifecycleService::DungeonLifecycleService(
    DungeonManager& dungeonManager,
    DungeonUdpManager& udpManager,
    std::chrono::milliseconds reconnectGrace)
    : dungeonManager_(dungeonManager),
      udpManager_(udpManager),
      reconnectGrace_(reconnectGrace)
{
    if (reconnectGrace_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument(
            "Dungeon reconnect grace must be positive");
    }
}

DungeonSessionRegistration DungeonLifecycleService::RegisterPlayerSession(
    SessionId sessionId,
    PlayerId playerId)
{
    const RegisterDungeonSessionResult registration =
        dungeonManager_.RegisterPlayerSession(
            sessionId,
            playerId,
            std::chrono::steady_clock::now(),
            reconnectGrace_);

    DungeonSessionRegistration result{
        registration.status,
        registration.dungeonId,
        registration.replacedSessionId,
        std::nullopt};

    if (registration.status !=
        RegisterDungeonSessionStatus::Reconnected)
    {
        return result;
    }

    result.freshUdpToken = udpManager_.ReplaceParticipant(
        registration.dungeonId,
        registration.replacedSessionId,
        sessionId);

    if (!result.freshUdpToken.has_value())
    {
        const bool rolledBack =
            registration.disconnectedAt.has_value() &&
            dungeonManager_.RollbackPlayerReconnect(
                registration.dungeonId,
                playerId,
                sessionId,
                registration.replacedSessionId,
                registration.disconnectedAt.value());

        if (!rolledBack)
        {
            dungeonManager_.DisconnectPlayerSession(sessionId);
        }
    }

    return result;
}

bool DungeonLifecycleService::DisconnectPlayerSession(SessionId sessionId)
{
    const auto dungeonId =
        dungeonManager_.DisconnectPlayerSession(sessionId);
    if (!dungeonId.has_value())
    {
        return false;
    }

    udpManager_.DisconnectParticipant(dungeonId.value(), sessionId);
    return true;
}

DungeonAbandonmentSweep
DungeonLifecycleService::SweepAbandonedParticipants(
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds reconnectGrace,
    std::chrono::milliseconds maxDungeonLifetime)
{
    DungeonAbandonmentSweep sweep =
        dungeonManager_.SweepAbandonedParticipants(
            now,
            reconnectGrace,
            maxDungeonLifetime);

    for (const AbandonedDungeonParticipant& participant :
         sweep.removedParticipants)
    {
        udpManager_.RemoveParticipant(
            participant.dungeonId,
            participant.sessionId);
    }

    for (DungeonId dungeonId : sweep.releasedDungeons)
    {
        udpManager_.Release(dungeonId);
    }

    return sweep;
}

bool DungeonLifecycleService::CancelWaitingDungeon(DungeonId dungeonId)
{
    if (!dungeonManager_.CancelDungeon(dungeonId))
    {
        return false;
    }

    udpManager_.Release(dungeonId);
    return true;
}

bool DungeonLifecycleService::FinishDungeon(DungeonId dungeonId)
{
    if (!dungeonManager_.FinishDungeon(dungeonId))
    {
        return false;
    }

    udpManager_.Release(dungeonId);
    return true;
}
} // namespace dnf
