#include "DungeonUdpManager.h"

#include <boost/system/error_code.hpp>

#include <utility>

namespace dnf
{
using boost::asio::ip::udp;

DungeonUdpManager::DungeonUdpManager(boost::asio::io_context& ioContext)
    : ioContext_(ioContext)
{
}

std::optional<std::uint16_t> DungeonUdpManager::Allocate(
    DungeonId dungeonId)
{
    std::lock_guard lock(mutex_);

    if (dungeonId == 0 || sockets_.contains(dungeonId))
    {
        return std::nullopt;
    }

    auto socket = std::make_unique<udp::socket>(ioContext_);
    socket->open(udp::v4());

    // 포트 0을 사용하면 운영체제가 현재 비어 있는 포트를 선택한다.
    socket->bind(udp::endpoint(udp::v4(), 0));
    const std::uint16_t port = socket->local_endpoint().port();

    sockets_.emplace(dungeonId, std::move(socket));
    return port;
}

std::optional<std::uint16_t> DungeonUdpManager::FindPort(
    DungeonId dungeonId) const
{
    std::lock_guard lock(mutex_);

    const auto socketIt = sockets_.find(dungeonId);
    if (socketIt == sockets_.end())
    {
        return std::nullopt;
    }

    return socketIt->second->local_endpoint().port();
}

bool DungeonUdpManager::Release(DungeonId dungeonId)
{
    std::lock_guard lock(mutex_);

    const auto socketIt = sockets_.find(dungeonId);
    if (socketIt == sockets_.end())
    {
        return false;
    }

    boost::system::error_code error;
    socketIt->second->close(error);
    sockets_.erase(socketIt);
    return !error;
}

std::size_t DungeonUdpManager::AllocationCount() const
{
    std::lock_guard lock(mutex_);
    return sockets_.size();
}
} // namespace dnf
