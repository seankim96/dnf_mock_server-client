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
        allocations_.contains(dungeonId))
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

    auto socket = std::make_unique<udp::socket>(ioContext_);
    boost::system::error_code error;
    socket->open(udp::v4(), error);
    if (error)
    {
        return std::nullopt;
    }

    // 포트 0을 사용하면 운영체제가 현재 비어 있는 포트를 선택한다.
    socket->bind(udp::endpoint(udp::v4(), 0), error);
    if (error)
    {
        return std::nullopt;
    }

    const udp::endpoint localEndpoint = socket->local_endpoint(error);
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

    allocations_.emplace(
        dungeonId,
        Allocation{std::move(socket), std::move(tokens)});
    return localEndpoint.port();
}

std::optional<std::uint16_t> DungeonUdpManager::FindPort(
    DungeonId dungeonId) const
{
    std::lock_guard lock(mutex_);

    const auto allocationIt = allocations_.find(dungeonId);
    if (allocationIt == allocations_.end())
    {
        return std::nullopt;
    }

    return allocationIt->second.socket->local_endpoint().port();
}

std::optional<DungeonUdpToken> DungeonUdpManager::FindToken(
    DungeonId dungeonId,
    SessionId sessionId) const
{
    std::lock_guard lock(mutex_);

    const auto allocationIt = allocations_.find(dungeonId);
    if (allocationIt == allocations_.end())
    {
        return std::nullopt;
    }

    const auto tokenIt = allocationIt->second.tokens.find(sessionId);
    if (tokenIt == allocationIt->second.tokens.end())
    {
        return std::nullopt;
    }

    return tokenIt->second;
}

bool DungeonUdpManager::Release(DungeonId dungeonId)
{
    std::lock_guard lock(mutex_);

    const auto allocationIt = allocations_.find(dungeonId);
    if (allocationIt == allocations_.end())
    {
        return false;
    }

    boost::system::error_code error;
    allocationIt->second.socket->close(error);
    allocations_.erase(allocationIt);
    return !error;
}

std::size_t DungeonUdpManager::AllocationCount() const
{
    std::lock_guard lock(mutex_);
    return allocations_.size();
}
} // namespace dnf
