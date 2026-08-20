#pragma once

#include <boost/asio/thread_pool.hpp>

#include <cstddef>
#include <functional>

namespace dnf
{
class DatabaseExecutor
{
public:
    explicit DatabaseExecutor(std::size_t workerCount = 2);
    ~DatabaseExecutor();

    DatabaseExecutor(const DatabaseExecutor&) = delete;
    DatabaseExecutor& operator=(const DatabaseExecutor&) = delete;

    void Post(std::function<void()> task);

private:
    boost::asio::thread_pool workers_;
};
} // namespace dnf
