#pragma once

#include "DungeonInputProcessor.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/system/error_code.hpp>

#include <cstdint>

namespace dnf
{
constexpr int DUNGEON_TICKS_PER_SECOND = 30;

class DungeonTickService
{
public:
    DungeonTickService(
        boost::asio::io_context& ioContext,
        DungeonManager& dungeonManager,
        DungeonUdpManager& udpManager);

    void Start();
    void Stop();

    bool IsRunning() const;
    std::uint64_t TickCount() const;

private:
    void ScheduleNextTick();
    void HandleTick(const boost::system::error_code& error);

    boost::asio::steady_timer timer_;
    DungeonManager& dungeonManager_;
    DungeonUdpManager& udpManager_;
    DungeonInputProcessor inputProcessor_;

    bool running_ = false;
    std::uint64_t tickCount_ = 0;
};
} // namespace dnf
