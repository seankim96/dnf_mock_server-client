#pragma once

#include <boost/asio/thread_pool.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>

namespace dnf
{
struct DatabaseExecutorStats
{
    bool acceptingTasks = true;
    std::size_t queueDepth = 0;
    std::size_t activeTasks = 0;
    std::size_t peakQueueDepth = 0;
    std::uint64_t acceptedTasks = 0;
    std::uint64_t rejectedTasks = 0;
    std::uint64_t completedTasks = 0;
    std::uint64_t failedTasks = 0;
    std::chrono::nanoseconds totalQueueWait{0};
    std::chrono::nanoseconds totalExecutionTime{0};
};

class DatabaseExecutor
{
public:
    explicit DatabaseExecutor(
        std::size_t workerCount = 2,
        std::size_t maxQueuedTasks = 256);
    ~DatabaseExecutor();

    DatabaseExecutor(const DatabaseExecutor&) = delete;
    DatabaseExecutor& operator=(const DatabaseExecutor&) = delete;

    bool TryPost(std::function<void()> task);
    void Post(std::function<void()> task);
    void DrainAndStop();

    DatabaseExecutorStats Stats() const;
    std::size_t MaxQueuedTasks() const;

private:
    const std::size_t maxQueuedTasks_;
    boost::asio::thread_pool workers_;
    mutable std::mutex lifecycleMutex_;
    std::condition_variable lifecycleCondition_;
    mutable std::mutex statsMutex_;
    DatabaseExecutorStats stats_;
    bool acceptingTasks_ = true;
    bool stopping_ = false;
    bool joined_ = false;
};
} // namespace dnf
