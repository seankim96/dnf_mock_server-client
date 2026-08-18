#pragma once

#include "DungeonInstance.h"
#include "DungeonUdpTypes.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <unordered_map>
#include <vector>

namespace dnf
{
class DungeonUdpManager
{
public:
    explicit DungeonUdpManager(boost::asio::io_context& ioContext);

    std::optional<std::uint16_t> Allocate(
        DungeonId dungeonId,
        const std::vector<SessionId>& participants);
    std::optional<std::uint16_t> FindPort(DungeonId dungeonId) const;
    std::optional<DungeonUdpToken> FindToken(
        DungeonId dungeonId,
        SessionId sessionId) const;
    bool Release(DungeonId dungeonId);
    std::size_t AllocationCount() const;

private:
    struct Allocation
    {
        std::unique_ptr<boost::asio::ip::udp::socket> socket;
        std::unordered_map<SessionId, DungeonUdpToken> tokens;
    };

    boost::asio::io_context& ioContext_;
    std::random_device randomDevice_;

    mutable std::mutex mutex_;
    std::unordered_map<DungeonId, Allocation> allocations_;
};
} // namespace dnf
