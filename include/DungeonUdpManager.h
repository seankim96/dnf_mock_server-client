#pragma once

#include "DungeonInstance.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/udp.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace dnf
{
class DungeonUdpManager
{
public:
    explicit DungeonUdpManager(boost::asio::io_context& ioContext);

    std::optional<std::uint16_t> Allocate(DungeonId dungeonId);
    std::optional<std::uint16_t> FindPort(DungeonId dungeonId) const;
    bool Release(DungeonId dungeonId);
    std::size_t AllocationCount() const;

private:
    boost::asio::io_context& ioContext_;

    mutable std::mutex mutex_;
    std::unordered_map<
        DungeonId,
        std::unique_ptr<boost::asio::ip::udp::socket>> sockets_;
};
} // namespace dnf
