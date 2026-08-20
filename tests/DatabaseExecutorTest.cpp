#include "DatabaseExecutor.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/post.hpp>

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
void TestDatabaseTaskRunsOutsideIoThread()
{
    boost::asio::io_context ioContext;
    auto workGuard = boost::asio::make_work_guard(ioContext);
    dnf::DatabaseExecutor databaseExecutor(1);

    const std::thread::id ioThreadId = std::this_thread::get_id();
    std::thread::id databaseThreadId;
    std::thread::id completionThreadId;
    bool taskCompleted = false;

    databaseExecutor.Post(
        [&]
        {
            databaseThreadId = std::this_thread::get_id();

            boost::asio::post(
                ioContext,
                [&]
                {
                    completionThreadId = std::this_thread::get_id();
                    taskCompleted = true;
                    workGuard.reset();
                });
        });

    ioContext.run();

    assert(taskCompleted);
    assert(databaseThreadId != ioThreadId);
    assert(completionThreadId == ioThreadId);
}

void TestInvalidConfigurationIsRejected()
{
    bool zeroWorkerRejected = false;
    try
    {
        dnf::DatabaseExecutor invalidExecutor(0);
    }
    catch (const std::invalid_argument&)
    {
        zeroWorkerRejected = true;
    }
    assert(zeroWorkerRejected);

    dnf::DatabaseExecutor databaseExecutor(1);
    bool emptyTaskRejected = false;
    try
    {
        databaseExecutor.Post({});
    }
    catch (const std::invalid_argument&)
    {
        emptyTaskRejected = true;
    }
    assert(emptyTaskRejected);
}
} // namespace

int main()
{
    TestDatabaseTaskRunsOutsideIoThread();
    TestInvalidConfigurationIsRejected();

    std::cout << "All database executor tests passed.\n";
    return 0;
}
