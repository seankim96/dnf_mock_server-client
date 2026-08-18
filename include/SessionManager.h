#pragma once

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
class PartyManager;
class TcpSession;

class SessionManager
{
public:
    SessionManager(
        ChannelManager& channelManager,
        PartyManager& partyManager);

    SessionId StartSession(boost::asio::ip::tcp::socket socket);
    void RemoveSession(SessionId sessionId);

    std::size_t ActiveSessionCount() const;

private:
    ChannelManager& channelManager_;
    PartyManager& partyManager_;
    std::atomic<SessionId> nextSessionId_{1};

    mutable std::mutex mutex_;
    std::unordered_map<SessionId, std::shared_ptr<TcpSession>> sessions_;
};
} // namespace dnf
