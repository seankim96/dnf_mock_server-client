#include "DungeonUdpManager.h"

#include <boost/system/error_code.hpp>

#include <random>
#include <unordered_set>
#include <utility>

namespace dnf
{
using boost::asio::ip::udp;

DungeonUdpManager::DungeonUdpManager(boost::asio::io_context& ioContext)
    : ioContext_(ioContext)
{
}

std::optional<std::uint16_t> DungeonUdpManager::Allocate(
    DungeonId dungeonId,
    const std::vector<SessionId>& participants)
{
    std::lock_guard lock(mutex_);

    if (dungeonId == 0 || participants.empty() ||
        sessions_.contains(dungeonId))
    {
        return std::nullopt;
    }

    std::unordered_set<SessionId> participantIds;
    for (SessionId sessionId : participants)
    {
        if (sessionId == 0 || !participantIds.insert(sessionId).second)
        {
            return std::nullopt;
        }
    }

    udp::socket socket(ioContext_);
    boost::system::error_code error;
    socket.open(udp::v4(), error);
    if (error)
    {
        return std::nullopt;
    }

    // 포트 0을 사용하면 운영체제가 현재 비어 있는 포트를 선택한다.
    socket.bind(udp::endpoint(udp::v4(), 0), error);
    if (error)
    {
        return std::nullopt;
    }

    const udp::endpoint localEndpoint = socket.local_endpoint(error);
    if (error)
    {
        return std::nullopt;
    }

    std::unordered_map<SessionId, DungeonUdpToken> tokens;
    std::unordered_set<DungeonUdpToken> usedTokens;

    for (SessionId sessionId : participants)
    {
        DungeonUdpToken token = 0;
        do
        {
            token = 0;
            for (std::size_t index = 0;
                 index < sizeof(DungeonUdpToken);
                 ++index)
            {
                token = (token << 8) |
                        static_cast<std::uint8_t>(randomDevice_());
            }
        } while (token == 0 || usedTokens.contains(token));

        usedTokens.insert(token);
        tokens.emplace(sessionId, token);
    }

    auto session = std::make_shared<DungeonUdpSession>(
        dungeonId,
        std::move(socket),
        std::move(tokens));
    sessions_.emplace(dungeonId, session);
    session->Start();
    return localEndpoint.port();
}

std::optional<std::uint16_t> DungeonUdpManager::FindPort(
    DungeonId dungeonId) const
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return std::nullopt;
    }

    return sessionIt->second->Port();
}

std::optional<DungeonUdpToken> DungeonUdpManager::FindToken(
    DungeonId dungeonId,
    SessionId sessionId) const
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return std::nullopt;
    }

    return sessionIt->second->FindToken(sessionId);
}

std::optional<udp::endpoint> DungeonUdpManager::FindEndpoint(
    DungeonId dungeonId,
    SessionId sessionId) const
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return std::nullopt;
    }

    return sessionIt->second->FindEndpoint(sessionId);
}

bool DungeonUdpManager::AllParticipantsAuthenticated(
    DungeonId dungeonId) const
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return false;
    }

    return sessionIt->second->AllParticipantsAuthenticated();
}

void DungeonUdpManager::RefreshAllActivity(DungeonId dungeonId)
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt != sessions_.end())
    {
        sessionIt->second->RefreshAllActivity();
    }
}

std::vector<SessionId> DungeonUdpManager::RemoveInactiveEndpoints(
    DungeonId dungeonId,
    std::chrono::milliseconds idleTimeout)
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return {};
    }

    return sessionIt->second->RemoveInactiveEndpoints(idleTimeout);
}

bool DungeonUdpManager::BroadcastSnapshot(
    DungeonId dungeonId,
    std::vector<std::uint8_t> bytes)
{
    std::shared_ptr<DungeonUdpSession> session;

    {
        std::lock_guard lock(mutex_);

        const auto sessionIt = sessions_.find(dungeonId);
        if (sessionIt == sessions_.end())
        {
            return false;
        }

        session = sessionIt->second;
    }

    return session->SendSnapshot(std::move(bytes));
}

bool DungeonUdpManager::TryPopInput(
    DungeonId dungeonId,
    AuthenticatedPlayerInput& output)
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return false;
    }

    return sessionIt->second->TryPopInput(output);
}

std::size_t DungeonUdpManager::PendingInputCount(
    DungeonId dungeonId) const
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return 0;
    }

    return sessionIt->second->PendingInputCount();
}

bool DungeonUdpManager::TryPopAttack(
    DungeonId dungeonId,
    AuthenticatedPlayerAttack& output)
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return false;
    }

    return sessionIt->second->TryPopAttack(output);
}

std::size_t DungeonUdpManager::PendingAttackCount(
    DungeonId dungeonId) const
{
    std::lock_guard lock(mutex_);

    const auto sessionIt = sessions_.find(dungeonId);
    if (sessionIt == sessions_.end())
    {
        return 0;
    }

    return sessionIt->second->PendingAttackCount();
}

bool DungeonUdpManager::Release(DungeonId dungeonId)
{
    std::shared_ptr<DungeonUdpSession> session;

    {
        std::lock_guard lock(mutex_);

        const auto sessionIt = sessions_.find(dungeonId);
        if (sessionIt == sessions_.end())
        {
            return false;
        }

        session = sessionIt->second;
        sessions_.erase(sessionIt);
    }

    session->Stop();
    return true;
}

std::size_t DungeonUdpManager::AllocationCount() const
{
    std::lock_guard lock(mutex_);
    return sessions_.size();
}
} // namespace dnf
