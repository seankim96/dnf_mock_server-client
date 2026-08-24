#pragma once

#include "NetworkSessionOptions.h"
#include "PlayerId.h"
#include "SessionId.h"

#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace dnf
{
class ChannelManager;
class DungeonManager;
class DungeonUdpManager;
class PartyManager;
class PlayerLoginService;
class TcpSession;

class SessionManager
{
public:
    SessionManager(
        ChannelManager& channelManager,
        PartyManager& partyManager,
        DungeonManager& dungeonManager,
        DungeonUdpManager& dungeonUdpManager,
        PlayerLoginService& playerLoginService,
        NetworkSessionOptions sessionOptions = {});

    SessionId StartSession(boost::asio::ip::tcp::socket socket);
    bool RegisterAuthenticatedPlayer(
        SessionId sessionId,
        PlayerId playerId);
    void RemoveSession(SessionId sessionId);
    void Stop();

    std::size_t ActiveSessionCount() const;

private:
    ChannelManager& channelManager_;
    PartyManager& partyManager_;
    DungeonManager& dungeonManager_;
    DungeonUdpManager& dungeonUdpManager_;
    PlayerLoginService& playerLoginService_;
    NetworkSessionOptions sessionOptions_;
    std::atomic<SessionId> nextSessionId_{1};

    mutable std::mutex mutex_;
    std::unordered_map<SessionId, std::shared_ptr<TcpSession>> sessions_;
    bool stopping_ = false;
};
} // namespace dnf
