#include "DungeonTickService.h"

#include "DungeonProtocol.h"

#include <boost/asio/error.hpp>
#include <boost/asio/dispatch.hpp>

#include <algorithm>
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
constexpr auto FINISHED_BROADCAST_DURATION = std::chrono::seconds(1);
} // namespace

DungeonTickService::DungeonTickService(
    boost::asio::io_context& ioContext,
    DungeonManager& dungeonManager,
    DungeonUdpManager& udpManager,
    const SkillCatalog& skillCatalog,
    std::chrono::milliseconds readyTimeout,
    std::chrono::milliseconds udpIdleTimeout,
    std::chrono::milliseconds reconnectGrace,
    std::chrono::milliseconds maxDungeonLifetime)
    : strand_(boost::asio::make_strand(ioContext)),
      timer_(strand_),
      dungeonManager_(dungeonManager),
      udpManager_(udpManager),
      movementProcessor_(dungeonManager, udpManager),
      combatProcessor_(dungeonManager, udpManager, skillCatalog),
      lifecycleService_(dungeonManager, udpManager, reconnectGrace),
      readyTimeout_(readyTimeout),
      udpIdleTimeout_(udpIdleTimeout),
      reconnectGrace_(reconnectGrace),
      maxDungeonLifetime_(maxDungeonLifetime)
{
    if (readyTimeout_ <= std::chrono::milliseconds::zero() ||
        udpIdleTimeout_ <= std::chrono::milliseconds::zero() ||
        reconnectGrace_ <= std::chrono::milliseconds::zero() ||
        maxDungeonLifetime_ <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument("Dungeon timeouts must be positive");
    }
}

void DungeonTickService::Start()
{
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true))
    {
        return;
    }

    boost::asio::dispatch(
        strand_,
        [this]
        {
            if (!running_.load())
            {
                return;
            }

            nextTick_ = std::chrono::steady_clock::now() + TICK_INTERVAL;
            ScheduleNextTick();
        });
}

void DungeonTickService::Stop()
{
    if (!running_.exchange(false))
    {
        return;
    }

    boost::asio::dispatch(
        strand_,
        [this]
        {
            timer_.cancel();
            waitingSince_.clear();
            finishedSince_.clear();
        });
}

bool DungeonTickService::IsRunning() const
{
    return running_.load();
}

std::uint64_t DungeonTickService::TickCount() const
{
    return Stats().processedTickCount;
}

DungeonTickStats DungeonTickService::Stats() const
{
    std::lock_guard lock(statsMutex_);
    return stats_;
}

void DungeonTickService::ScheduleNextTick()
{
    if (!running_.load())
    {
        return;
    }

    timer_.expires_at(nextTick_);
    timer_.async_wait(
        [this](const boost::system::error_code& error)
        {
            HandleTick(error);
        });
}

void DungeonTickService::AdvanceDeadline()
{
    nextTick_ += TICK_INTERVAL;

    const auto now = std::chrono::steady_clock::now();
    if (nextTick_ > now)
    {
        return;
    }

    const auto overdueTickCount =
        static_cast<std::uint64_t>((now - nextTick_) / TICK_INTERVAL) + 1;
    if (overdueTickCount <= MAX_DUNGEON_CATCH_UP_TICKS)
    {
        return;
    }

    const std::uint64_t skippedTickCount =
        overdueTickCount - MAX_DUNGEON_CATCH_UP_TICKS;

    // 오래된 deadline은 버리고 설정된 개수만 즉시 따라잡는다.
    nextTick_ += TICK_INTERVAL * skippedTickCount;

    std::lock_guard lock(statsMutex_);
    stats_.missedTickCount += skippedTickCount;
}

void DungeonTickService::HandleTick(
    const boost::system::error_code& error)
{
    if (!running_.load() ||
        error == boost::asio::error::operation_aborted)
    {
        return;
    }

    if (error)
    {
        Stop();
        return;
    }

    const auto processingStartedAt = std::chrono::steady_clock::now();
    const auto lateness = processingStartedAt > nextTick_
        ? std::chrono::duration_cast<std::chrono::nanoseconds>(
              processingStartedAt - nextTick_)
        : std::chrono::nanoseconds::zero();

    std::uint64_t tickCount = 0;
    {
        std::lock_guard lock(statsMutex_);
        ++stats_.processedTickCount;
        stats_.lastLateness = lateness;
        stats_.maxLateness = std::max(stats_.maxLateness, lateness);
        tickCount = stats_.processedTickCount;
    }

    udpManager_.SetServerTick(
        static_cast<std::uint32_t>(tickCount));

    const auto now = std::chrono::steady_clock::now();
    const DungeonAbandonmentSweep abandonmentSweep =
        lifecycleService_.SweepAbandonedParticipants(
            now,
            reconnectGrace_,
            maxDungeonLifetime_);
    for (DungeonId releasedDungeonId :
         abandonmentSweep.releasedDungeons)
    {
        waitingSince_.erase(releasedDungeonId);
        finishedSince_.erase(releasedDungeonId);
    }

    const std::vector<DungeonId> waitingDungeonIds =
        dungeonManager_.WaitingDungeonIds();
    const std::unordered_set<DungeonId> waitingDungeonSet(
        waitingDungeonIds.begin(),
        waitingDungeonIds.end());

    for (DungeonId dungeonId : waitingDungeonIds)
    {
        const auto waitingDungeon =
            dungeonManager_.FindDungeon(dungeonId);
        if (waitingDungeon != nullptr &&
            waitingDungeon->HasDisconnectedParticipants())
        {
            // 재접속 유예 중에는 준비 타임아웃을 진행하지 않는다.
            waitingSince_.erase(dungeonId);
            continue;
        }

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
        movementProcessor_.Process(dungeonId, TICK_SECONDS);
        combatProcessor_.Process(dungeonId);

        const auto dungeon = dungeonManager_.FindDungeon(dungeonId);
        if (dungeon != nullptr)
        {
            dungeon->AdvanceRoomWaves();
            dungeon->TryFinishIfCleared();

            if (dungeon->State() == DungeonState::Finished)
            {
                finishedSince_.try_emplace(dungeonId, now);
            }
            else
            {
                udpManager_.BroadcastSnapshot(
                    dungeonId,
                    EncodeDungeonSnapshot(
                        *dungeon,
                        static_cast<std::uint32_t>(tickCount)));
            }
        }
    }

    for (DungeonId dungeonId : dungeonManager_.FinishedDungeonIds())
    {
        const auto dungeon = dungeonManager_.FindDungeon(dungeonId);
        if (dungeon == nullptr)
        {
            finishedSince_.erase(dungeonId);
            continue;
        }

        const auto [finishedIt, inserted] =
            finishedSince_.try_emplace(dungeonId, now);
        (void)inserted;

        udpManager_.BroadcastSnapshot(
            dungeonId,
            EncodeDungeonSnapshot(
                *dungeon,
                static_cast<std::uint32_t>(tickCount)));

        if (now - finishedIt->second >= FINISHED_BROADCAST_DURATION)
        {
            lifecycleService_.FinishDungeon(dungeonId);
            finishedSince_.erase(finishedIt);
        }
    }

    const auto processingTime =
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - processingStartedAt);
    {
        std::lock_guard lock(statsMutex_);
        stats_.lastProcessingTime = processingTime;
        stats_.maxProcessingTime =
            std::max(stats_.maxProcessingTime, processingTime);
    }

    AdvanceDeadline();
    ScheduleNextTick();
}
} // namespace dnf
