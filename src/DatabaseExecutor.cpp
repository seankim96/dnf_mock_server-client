#include "DatabaseExecutor.h"

#include <boost/asio/post.hpp>

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
} // namespace

DatabaseExecutor::DatabaseExecutor(std::size_t workerCount)
    : workers_(ValidateWorkerCount(workerCount))
{
}

DatabaseExecutor::~DatabaseExecutor()
{
    workers_.join();
}

void DatabaseExecutor::Post(std::function<void()> task)
{
    if (!task)
    {
        throw std::invalid_argument("Database task must not be empty");
    }

    boost::asio::post(workers_, std::move(task));
}
} // namespace dnf
