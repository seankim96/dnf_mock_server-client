#include "DungeonTickService.h"

#include "DungeonProtocol.h"

#include <boost/asio/error.hpp>

#include <chrono>
#include <stdexcept>
#include <unordered_set>

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
    DungeonUdpManager& udpManager,
    std::chrono::milliseconds readyTimeout,
    std::chrono::milliseconds udpIdleTimeout)
    : timer_(ioContext),
      dungeonManager_(dungeonManager),
      udpManager_(udpManager),
      inputProcessor_(dungeonManager, udpManager),
      lifecycleService_(dungeonManager, udpManager),
      readyTimeout_(readyTimeout),
      udpIdleTimeout_(udpIdleTimeout)
{
    if (readyTimeout_ <= std::chrono::milliseconds::zero() ||
        udpIdleTimeout_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Dungeon timeouts must be positive");
    }
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

    const auto now = std::chrono::steady_clock::now();
    const std::vector<DungeonId> waitingDungeonIds =
        dungeonManager_.WaitingDungeonIds();
    const std::unordered_set<DungeonId> waitingDungeonSet(
        waitingDungeonIds.begin(),
        waitingDungeonIds.end());

    for (DungeonId dungeonId : waitingDungeonIds)
    {
        if (udpManager_.AllParticipantsAuthenticated(dungeonId))
        {
            if (dungeonManager_.StartDungeon(dungeonId))
            {
                udpManager_.RefreshAllActivity(dungeonId);
            }

            waitingSince_.erase(dungeonId);
            continue;
        }

        const auto [waitingIt, inserted] =
            waitingSince_.try_emplace(dungeonId, now);

        if (!inserted && now - waitingIt->second >= readyTimeout_)
        {
            lifecycleService_.CancelWaitingDungeon(dungeonId);

            waitingSince_.erase(waitingIt);
        }
    }

    for (auto waitingIt = waitingSince_.begin();
         waitingIt != waitingSince_.end();)
    {
        if (!waitingDungeonSet.contains(waitingIt->first))
        {
            waitingIt = waitingSince_.erase(waitingIt);
        }
        else
        {
            ++waitingIt;
        }
    }

    for (DungeonId dungeonId : dungeonManager_.RunningDungeonIds())
    {
        udpManager_.RemoveInactiveEndpoints(
            dungeonId,
            udpIdleTimeout_);
        inputProcessor_.Process(dungeonId, TICK_SECONDS);

        const auto dungeon = dungeonManager_.FindDungeon(dungeonId);
        if (dungeon != nullptr)
        {
            dungeon->AdvanceRoomWaves();
            udpManager_.BroadcastSnapshot(
                dungeonId,
                EncodeDungeonSnapshot(
                    *dungeon,
                    static_cast<std::uint32_t>(tickCount_)));
        }
    }

    ScheduleNextTick();
}
} // namespace dnf
