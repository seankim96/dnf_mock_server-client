#include "SessionManager.h"

#include "ChannelManager.h"
#include "DungeonLifecycleService.h"
#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "PartyManager.h"
#include "PlayerLoginService.h"
#include "TcpSession.h"

#include <boost/system/error_code.hpp>

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace dnf
{
SessionManager::SessionManager(
    ChannelManager& channelManager,
    PartyManager& partyManager,
    DungeonManager& dungeonManager,
    DungeonUdpManager& dungeonUdpManager,
    PlayerLoginService& playerLoginService,
    NetworkSessionOptions sessionOptions)
    : channelManager_(channelManager),
      partyManager_(partyManager),
      dungeonManager_(dungeonManager),
      dungeonUdpManager_(dungeonUdpManager),
      playerLoginService_(playerLoginService),
      sessionOptions_(std::move(sessionOptions))
{
    if (!sessionOptions_.IsValid())
    {
        throw std::invalid_argument(
            "TCP session options are invalid");
    }
}

SessionId SessionManager::StartSession(boost::asio::ip::tcp::socket socket)
{
    SessionId sessionId = 0;
    std::shared_ptr<TcpSession> session;
    std::size_t activeSessionCount = 0;

    {
        std::lock_guard lock(mutex_);

        if (stopping_)
        {
            boost::system::error_code ignoredError;
            socket.close(ignoredError);
            throw std::runtime_error(
                "Session manager is stopping");
        }

        sessionId = nextSessionId_.fetch_add(1);
        session = std::make_shared<TcpSession>(
            sessionId,
            std::move(socket),
            *this,
            channelManager_,
            partyManager_,
            dungeonManager_,
            dungeonUdpManager_,
            playerLoginService_,
            sessionOptions_);
        sessions_.emplace(sessionId, session);
        activeSessionCount = sessions_.size();
    }

    std::cout << "Session created id=" << sessionId
              << " active=" << activeSessionCount << '\n';

    session->Start();
    return sessionId;
}

bool SessionManager::RegisterAuthenticatedPlayer(
    SessionId sessionId,
    PlayerId playerId)
{
    DungeonLifecycleService lifecycleService(
        dungeonManager_,
        dungeonUdpManager_);
    const DungeonSessionRegistration registration =
        lifecycleService.RegisterPlayerSession(sessionId, playerId);

    if (registration.status ==
        RegisterDungeonSessionStatus::InvalidIdentity)
    {
        return false;
    }

    if (registration.status ==
        RegisterDungeonSessionStatus::Reconnected)
    {
        if (!registration.freshUdpToken.has_value())
        {
            std::cerr << "Dungeon reconnect UDP rotation failed"
                      << " sessionId=" << sessionId
                      << " dungeonId=" << registration.dungeonId
                      << '\n';
            return false;
        }

        std::cout << "Dungeon player reconnected"
                  << " sessionId=" << sessionId
                  << " dungeonId=" << registration.dungeonId
                  << '\n';
    }

    return true;
}

void SessionManager::Stop()
{
    std::vector<std::shared_ptr<TcpSession>> sessions;

    {
        std::lock_guard lock(mutex_);
        if (stopping_)
        {
            return;
        }

        stopping_ = true;
        sessions.reserve(sessions_.size());
        for (const auto& [sessionId, session] : sessions_)
        {
            (void)sessionId;
            sessions.push_back(session);
        }
    }

    for (const auto& session : sessions)
    {
        session->Stop();
    }
}

void SessionManager::RemoveSession(SessionId sessionId)
{
    DungeonLifecycleService lifecycleService(
        dungeonManager_,
        dungeonUdpManager_);
    lifecycleService.DisconnectPlayerSession(sessionId);

    channelManager_.LeaveChannel(sessionId);
    partyManager_.LeaveParty(sessionId);

    std::size_t activeSessionCount = 0;

    {
        std::lock_guard lock(mutex_);
        sessions_.erase(sessionId);
        activeSessionCount = sessions_.size();
    }

    std::cout << "Session disconnected id=" << sessionId
              << " active=" << activeSessionCount << '\n';
}

std::size_t SessionManager::ActiveSessionCount() const
{
    std::lock_guard lock(mutex_);
    return sessions_.size();
}
} // namespace dnf
