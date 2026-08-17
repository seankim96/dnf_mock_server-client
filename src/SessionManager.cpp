#include "SessionManager.h"

#include "TcpSession.h"

#include <iostream>
#include <utility>

namespace dnf
{
SessionId SessionManager::StartSession(boost::asio::ip::tcp::socket socket)
{
    const SessionId sessionId = nextSessionId_.fetch_add(1);
    auto session = std::make_shared<TcpSession>(
        sessionId, std::move(socket), *this);

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
