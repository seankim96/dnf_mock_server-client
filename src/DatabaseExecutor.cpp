#include "DatabaseExecutor.h"

#include <boost/asio/post.hpp>

#include <chrono>
#include <stdexcept>
#include <utility>

namespace dnf
{
namespace
{
std::size_t ValidateWorkerCount(std::size_t workerCount)
{
    if (workerCount == 0)
    {
        throw std::invalid_argument(
            "Database worker count must be positive");
    }

    return workerCount;
}

std::size_t ValidateQueueLimit(std::size_t maxQueuedTasks)
{
    if (maxQueuedTasks == 0)
    {
        throw std::invalid_argument(
            "Database queue limit must be positive");
    }

    return maxQueuedTasks;
}
} // namespace

DatabaseExecutor::DatabaseExecutor(
    std::size_t workerCount,
    std::size_t maxQueuedTasks)
    : maxQueuedTasks_(ValidateQueueLimit(maxQueuedTasks)),
      workers_(ValidateWorkerCount(workerCount))
{
}

DatabaseExecutor::~DatabaseExecutor()
{
    DrainAndStop();
}

bool DatabaseExecutor::TryPost(std::function<void()> task)
{
    if (!task)
    {
        throw std::invalid_argument("Database task must not be empty");
    }

    std::lock_guard lifecycleLock(lifecycleMutex_);
    if (!acceptingTasks_)
    {
        std::lock_guard statsLock(statsMutex_);
        ++stats_.rejectedTasks;
        return false;
    }

    {
        std::lock_guard lock(statsMutex_);
        if (stats_.queueDepth >= maxQueuedTasks_)
        {
            ++stats_.rejectedTasks;
            return false;
        }

        ++stats_.queueDepth;
        ++stats_.acceptedTasks;
        if (stats_.queueDepth > stats_.peakQueueDepth)
        {
            stats_.peakQueueDepth = stats_.queueDepth;
        }
    }

    const auto queuedAt = std::chrono::steady_clock::now();

    try
    {
        boost::asio::post(
            workers_,
            [this,
             task = std::move(task),
             queuedAt]() mutable
            {
                const auto startedAt =
                    std::chrono::steady_clock::now();

                {
                    std::lock_guard lock(statsMutex_);
                    --stats_.queueDepth;
                    ++stats_.activeTasks;
                    stats_.totalQueueWait +=
                        startedAt - queuedAt;
                }

                bool failed = false;
                try
                {
                    task();
                }
                catch (...)
                {
                    failed = true;
                }

                const auto completedAt =
                    std::chrono::steady_clock::now();
                {
                    std::lock_guard lock(statsMutex_);
                    --stats_.activeTasks;
                    ++stats_.completedTasks;
                    if (failed)
                    {
                        ++stats_.failedTasks;
                    }
                    stats_.totalExecutionTime +=
                        completedAt - startedAt;
                }
            });
    }
    catch (...)
    {
        std::lock_guard lock(statsMutex_);
        --stats_.queueDepth;
        --stats_.acceptedTasks;
        ++stats_.rejectedTasks;
        throw;
    }

    return true;
}

void DatabaseExecutor::Post(std::function<void()> task)
{
    if (!TryPost(std::move(task)))
    {
        throw std::runtime_error("Database task queue is full");
    }
}

void DatabaseExecutor::DrainAndStop()
{
    {
        std::unique_lock lifecycleLock(lifecycleMutex_);
        if (joined_)
        {
            return;
        }

        if (stopping_)
        {
            lifecycleCondition_.wait(
                lifecycleLock,
                [this]
                {
                    return joined_;
                });
            return;
        }

        acceptingTasks_ = false;
        stopping_ = true;
    }

    {
        std::lock_guard statsLock(statsMutex_);
        stats_.acceptingTasks = false;
    }

    workers_.join();

    {
        std::lock_guard lifecycleLock(lifecycleMutex_);
        joined_ = true;
    }
    lifecycleCondition_.notify_all();
}

DatabaseExecutorStats DatabaseExecutor::Stats() const
{
    std::lock_guard lock(statsMutex_);
    return stats_;
}

std::size_t DatabaseExecutor::MaxQueuedTasks() const
{
    return maxQueuedTasks_;
}
} // namespace dnf
