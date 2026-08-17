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
class TcpSession;

class SessionManager
{
public:
    SessionId StartSession(boost::asio::ip::tcp::socket socket);
    void RemoveSession(SessionId sessionId);

    std::size_t ActiveSessionCount() const;

private:
    std::atomic<SessionId> nextSessionId_{1};

    mutable std::mutex mutex_;
    std::unordered_map<SessionId, std::shared_ptr<TcpSession>> sessions_;
};
} // namespace dnf
