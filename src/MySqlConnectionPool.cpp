#include "MySqlConnectionPool.h"

#include "DatabaseError.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/pool_params.hpp>
#include <boost/mysql/ssl_mode.hpp>
#include <boost/system/system_error.hpp>

#include <future>
#include <stdexcept>
#include <thread>
#include <utility>

namespace dnf
{
namespace
{
boost::mysql::ssl_mode ToBoostSslMode(MySqlTlsMode mode)
{
    switch (mode)
    {
    case MySqlTlsMode::Disabled:
        return boost::mysql::ssl_mode::disable;
    case MySqlTlsMode::Preferred:
        return boost::mysql::ssl_mode::enable;
    case MySqlTlsMode::Required:
        return boost::mysql::ssl_mode::require;
    }

    throw std::invalid_argument("Unknown MySQL TLS mode");
}

boost::mysql::pool_params MakePoolParams(
    MySqlConnectionPoolOptions options)
{
    boost::mysql::pool_params params;
    params.server_address.emplace_host_and_port(
        std::move(options.host),
        options.port);
    params.username = std::move(options.username);
    params.password = std::move(options.password);
    params.database = std::move(options.database);
    params.ssl = ToBoostSslMode(options.tlsMode);
    params.initial_size = options.initialSize;
    params.max_size = options.maxSize;
    params.connect_timeout = options.connectTimeout;
    params.thread_safe = true;
    return params;
}
} // namespace

void ValidateMySqlConnectionPoolOptions(
    const MySqlConnectionPoolOptions& options)
{
    if (options.host.empty())
    {
        throw std::invalid_argument("MySQL host must not be empty");
    }

    if (options.port == 0)
    {
        throw std::invalid_argument("MySQL port must not be zero");
    }

    if (options.username.empty())
    {
        throw std::invalid_argument("MySQL username must not be empty");
    }

    if (options.database.empty())
    {
        throw std::invalid_argument("MySQL database must not be empty");
    }

    if (options.maxSize == 0 ||
        options.maxSize > MAX_MYSQL_CONNECTION_POOL_SIZE)
    {
        throw std::invalid_argument("MySQL max pool size is out of range");
    }

    if (options.initialSize > options.maxSize)
    {
        throw std::invalid_argument(
            "MySQL initial pool size exceeds max size");
    }

    if (options.connectTimeout <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument(
            "MySQL connect timeout must be positive");
    }

    if (options.acquireTimeout <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument(
            "MySQL acquire timeout must be positive");
    }

    if (options.queryTimeout <= std::chrono::milliseconds::zero())
    {
        throw std::invalid_argument(
            "MySQL query timeout must be positive");
    }

    static_cast<void>(ToBoostSslMode(options.tlsMode));
}

class MySqlConnectionPool::Impl
{
public:
    Impl(
        MySqlConnectionPoolOptions options,
        std::chrono::milliseconds acquireTimeout)
        : workGuard_(boost::asio::make_work_guard(ioContext_)),
          pool_(ioContext_, MakePoolParams(std::move(options))),
          acquireTimeout_(acquireTimeout)
    {
        pool_.async_run(boost::asio::detached);
        ioThread_ = std::thread([this]
        {
            ioContext_.run();
        });
    }

    ~Impl()
    {
        pool_.cancel();
        workGuard_.reset();
        ioContext_.stop();

        if (ioThread_.joinable())
        {
            ioThread_.join();
        }
    }

    boost::mysql::pooled_connection Acquire()
    {
        std::future<boost::mysql::pooled_connection> future =
            boost::asio::co_spawn(
                pool_.get_executor(),
                [this]()
                    -> boost::asio::awaitable<
                        boost::mysql::pooled_connection>
                {
                    co_return co_await pool_.async_get_connection(
                        boost::asio::cancel_after(
                            acquireTimeout_,
                            boost::asio::use_awaitable));
                },
                boost::asio::use_future);

        try
        {
            return future.get();
        }
        catch (const boost::system::system_error& error)
        {
            throw DatabaseError(
                "MySQL connection acquisition failed: " +
                error.code().message());
        }
    }

private:
    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type> workGuard_;
    boost::mysql::connection_pool pool_;
    std::chrono::milliseconds acquireTimeout_;
    std::thread ioThread_;
};

MySqlConnectionPool::MySqlConnectionPool(
    MySqlConnectionPoolOptions options)
{
    ValidateMySqlConnectionPoolOptions(options);
    maxSize_ = options.maxSize;
    queryTimeout_ = options.queryTimeout;
    const std::chrono::milliseconds acquireTimeout =
        options.acquireTimeout;
    impl_ = std::make_unique<Impl>(
        std::move(options),
        acquireTimeout);
}

MySqlConnectionPool::~MySqlConnectionPool() = default;

boost::mysql::pooled_connection MySqlConnectionPool::Acquire()
{
    return impl_->Acquire();
}

std::size_t MySqlConnectionPool::MaxSize() const
{
    return maxSize_;
}

std::chrono::milliseconds MySqlConnectionPool::QueryTimeout() const
{
    return queryTimeout_;
}
} // namespace dnf
