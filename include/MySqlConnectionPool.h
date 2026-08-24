#pragma once

#include <boost/mysql/connection_pool.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace dnf
{
constexpr std::size_t MAX_MYSQL_CONNECTION_POOL_SIZE = 64;

enum class MySqlTlsMode : std::uint8_t
{
    Disabled,
    Preferred,
    Required
};

struct MySqlConnectionPoolOptions
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 3306;
    std::string username;
    std::string password;
    std::string database;
    std::size_t initialSize = 1;
    std::size_t maxSize = 4;
    std::chrono::milliseconds connectTimeout{5000};
    std::chrono::milliseconds acquireTimeout{5000};
    std::chrono::milliseconds queryTimeout{5000};
    MySqlTlsMode tlsMode = MySqlTlsMode::Required;
};

void ValidateMySqlConnectionPoolOptions(
    const MySqlConnectionPoolOptions& options);

class MySqlConnectionPool
{
public:
    explicit MySqlConnectionPool(MySqlConnectionPoolOptions options);
    ~MySqlConnectionPool();

    MySqlConnectionPool(const MySqlConnectionPool&) = delete;
    MySqlConnectionPool& operator=(const MySqlConnectionPool&) = delete;

    boost::mysql::pooled_connection Acquire();
    std::size_t MaxSize() const;
    std::chrono::milliseconds QueryTimeout() const;

private:
    class Impl;

    std::size_t maxSize_ = 0;
    std::chrono::milliseconds queryTimeout_{0};
    std::unique_ptr<Impl> impl_;
};
} // namespace dnf
