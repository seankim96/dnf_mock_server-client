#include "DungeonTickService.h"

#include <boost/asio/error.hpp>

#include <chrono>

namespace dnf
{
namespace
{
constexpr auto TICK_INTERVAL =
    std::chrono::microseconds(1'000'000 / DUNGEON_TICKS_PER_SECOND);
constexpr float TICK_SECONDS =
    1.0f / static_cast<float>(DUNGEON_TICKS_PER_SECOND);
} // namespace

DungeonTickService::DungeonTickService(
    boost::asio::io_context& ioContext,
    DungeonManager& dungeonManager,
    DungeonUdpManager& udpManager)
    : timer_(ioContext),
      dungeonManager_(dungeonManager),
      inputProcessor_(dungeonManager, udpManager)
{
}

void DungeonTickService::Start()
{
    if (running_)
    {
        return;
    }

    running_ = true;
    ScheduleNextTick();
}

void DungeonTickService::Stop()
{
    running_ = false;
    timer_.cancel();
}

bool DungeonTickService::IsRunning() const
{
    return running_;
}

std::uint64_t DungeonTickService::TickCount() const
{
    return tickCount_;
}

void DungeonTickService::ScheduleNextTick()
{
    if (!running_)
    {
        return;
    }

    timer_.expires_after(TICK_INTERVAL);
    timer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            HandleTick(error);
        });
}

void DungeonTickService::HandleTick(
    const boost::system::error_code& error)
{
    if (!running_ || error == boost::asio::error::operation_aborted)
    {
        return;
    }

    if (error)
    {
        Stop();
        return;
    }

    ++tickCount_;

    for (DungeonId dungeonId : dungeonManager_.RunningDungeonIds())
    {
        inputProcessor_.Process(dungeonId, TICK_SECONDS);
    }

    ScheduleNextTick();
}
} // namespace dnf
