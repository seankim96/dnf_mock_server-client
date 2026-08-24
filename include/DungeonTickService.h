#pragma once

#include "DungeonCombatProcessor.h"
#include "DungeonMovementProcessor.h"
#include "DungeonLifecycleService.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <unordered_map>

namespace dnf
{
constexpr int DUNGEON_TICKS_PER_SECOND = 30;
constexpr std::uint64_t MAX_DUNGEON_CATCH_UP_TICKS = 2;
constexpr auto DEFAULT_DUNGEON_READY_TIMEOUT = std::chrono::seconds(10);
constexpr auto DEFAULT_UDP_IDLE_TIMEOUT = std::chrono::seconds(5);
constexpr auto DEFAULT_DUNGEON_MAX_LIFETIME = std::chrono::hours(2);

struct DungeonTickStats
{
    std::uint64_t processedTickCount = 0;
    std::uint64_t missedTickCount = 0;
    std::chrono::nanoseconds lastLateness{0};
    std::chrono::nanoseconds maxLateness{0};
    std::chrono::nanoseconds lastProcessingTime{0};
    std::chrono::nanoseconds maxProcessingTime{0};
};

class DungeonTickService
{
public:
    DungeonTickService(
        boost::asio::io_context& ioContext,
        DungeonManager& dungeonManager,
        DungeonUdpManager& udpManager,
        const SkillCatalog& skillCatalog,
        std::chrono::milliseconds readyTimeout =
            DEFAULT_DUNGEON_READY_TIMEOUT,
        std::chrono::milliseconds udpIdleTimeout =
            DEFAULT_UDP_IDLE_TIMEOUT,
        std::chrono::milliseconds reconnectGrace =
            DEFAULT_DUNGEON_RECONNECT_GRACE,
        std::chrono::milliseconds maxDungeonLifetime =
            DEFAULT_DUNGEON_MAX_LIFETIME);

    void Start();
    void Stop();

    bool IsRunning() const;
    std::uint64_t TickCount() const;
    DungeonTickStats Stats() const;

private:
    void ScheduleNextTick();
    void AdvanceDeadline();
    void HandleTick(const boost::system::error_code& error);

    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
    boost::asio::steady_timer timer_;
    DungeonManager& dungeonManager_;
    DungeonUdpManager& udpManager_;
    DungeonMovementProcessor movementProcessor_;
    DungeonCombatProcessor combatProcessor_;
    DungeonLifecycleService lifecycleService_;
    std::chrono::milliseconds readyTimeout_;
    std::chrono::milliseconds udpIdleTimeout_;
    std::chrono::milliseconds reconnectGrace_;
    std::chrono::milliseconds maxDungeonLifetime_;
    std::unordered_map<
        DungeonId,
        std::chrono::steady_clock::time_point> waitingSince_;
    std::unordered_map<
        DungeonId,
        std::chrono::steady_clock::time_point> finishedSince_;

    std::chrono::steady_clock::time_point nextTick_;
    std::atomic<bool> running_{false};
    mutable std::mutex statsMutex_;
    DungeonTickStats stats_;
};
} // namespace dnf
