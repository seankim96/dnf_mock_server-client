#include "DatabaseExecutor.h"

#include <cassert>
#include <future>
#include <iostream>
#include <stdexcept>

namespace
{
void TestQueueLimitRejectsWithoutBlocking()
{
    dnf::DatabaseExecutor executor(1, 1);
    std::promise<void> firstStarted;
    std::promise<void> release;
    const std::shared_future<void> releaseFuture =
        release.get_future().share();

    assert(executor.TryPost(
        [&firstStarted, releaseFuture]
        {
            firstStarted.set_value();
            releaseFuture.wait();
        }));
    firstStarted.get_future().wait();

    assert(executor.TryPost(
        [releaseFuture]
        {
            releaseFuture.wait();
        }));
    assert(!executor.TryPost([] {}));

    const dnf::DatabaseExecutorStats saturated = executor.Stats();
    assert(saturated.acceptingTasks);
    assert(saturated.activeTasks == 1);
    assert(saturated.queueDepth == 1);
    assert(saturated.peakQueueDepth == 1);
    assert(saturated.acceptedTasks == 2);
    assert(saturated.rejectedTasks == 1);

    release.set_value();
    executor.DrainAndStop();

    const dnf::DatabaseExecutorStats drained = executor.Stats();
    assert(!drained.acceptingTasks);
    assert(drained.activeTasks == 0);
    assert(drained.queueDepth == 0);
    assert(drained.completedTasks == 2);
    assert(drained.totalExecutionTime.count() >= 0);
    assert(drained.totalQueueWait.count() >= 0);

    assert(!executor.TryPost([] {}));
    assert(executor.Stats().rejectedTasks == 2);
    executor.DrainAndStop();
}

void TestTaskFailureIsContainedAndCounted()
{
    dnf::DatabaseExecutor executor(1, 1);
    assert(executor.TryPost(
        []
        {
            throw std::runtime_error("expected test failure");
        }));

    executor.DrainAndStop();
    const dnf::DatabaseExecutorStats stats = executor.Stats();
    assert(stats.completedTasks == 1);
    assert(stats.failedTasks == 1);
}

void TestInvalidQueueLimitIsRejected()
{
    bool rejected = false;
    try
    {
        dnf::DatabaseExecutor executor(1, 0);
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }
    assert(rejected);
}
} // namespace

int main()
{
    TestQueueLimitRejectsWithoutBlocking();
    TestTaskFailureIsContainedAndCounted();
    TestInvalidQueueLimitIsRejected();

    std::cout << "All database backpressure tests passed.\n";
    return 0;
}
