#pragma once

#include "DungeonInstance.h"
#include "DungeonUdpSession.h"
#include "DungeonUdpTypes.h"

#include <boost/asio/io_context.hpp>

#include <chrono>
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
    std::optional<boost::asio::ip::udp::endpoint> FindEndpoint(
        DungeonId dungeonId,
        SessionId sessionId) const;
    bool AllParticipantsAuthenticated(DungeonId dungeonId) const;
    void RefreshAllActivity(DungeonId dungeonId);
    std::vector<SessionId> RemoveInactiveEndpoints(
        DungeonId dungeonId,
        std::chrono::milliseconds idleTimeout);
    bool BroadcastSnapshot(
        DungeonId dungeonId,
        std::vector<std::uint8_t> bytes);
    bool TryPopInput(
        DungeonId dungeonId,
        AuthenticatedPlayerInput& output);
    std::size_t PendingInputCount(DungeonId dungeonId) const;
    bool Release(DungeonId dungeonId);
    std::size_t AllocationCount() const;

private:
    boost::asio::io_context& ioContext_;
    std::random_device randomDevice_;

    mutable std::mutex mutex_;
    std::unordered_map<DungeonId, std::shared_ptr<DungeonUdpSession>> sessions_;
};
} // namespace dnf
