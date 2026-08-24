#include "SessionManager.h"

#include "ChannelManager.h"
#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "PartyManager.h"
#include "PlayerLoginService.h"
#include "TcpSession.h"

#include <iostream>
#include <stdexcept>
#include <utility>

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
    const SessionId sessionId = nextSessionId_.fetch_add(1);
    auto session = std::make_shared<TcpSession>(
        sessionId,
        std::move(socket),
        *this,
        channelManager_,
        partyManager_,
        dungeonManager_,
        dungeonUdpManager_,
        playerLoginService_,
        sessionOptions_);

    std::size_t activeSessionCount = 0;

    {
        std::lock_guard lock(mutex_);
        sessions_.emplace(sessionId, session);
        activeSessionCount = sessions_.size();
    }

    std::cout << "Session created id=" << sessionId
              << " active=" << activeSessionCount << '\n';

    session->Start();
    return sessionId;
}

void SessionManager::RemoveSession(SessionId sessionId)
{
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
