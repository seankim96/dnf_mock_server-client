#include "MySqlConnectionPool.h"
#include "MySqlPlayerRepository.h"

#include <boost/mysql/common_server_errc.hpp>

#include <chrono>
#include <functional>
#include <stdexcept>

namespace
{
dnf::MySqlConnectionPoolOptions ValidOptions()
{
    dnf::MySqlConnectionPoolOptions options;
    options.username = "dnf_test";
    options.database = "dnf_test";
    return options;
}

void Check(bool condition)
{
    if (!condition)
    {
        throw std::runtime_error("MySQL pool config test failed");
    }
}

void ExpectInvalid(
    const std::function<void(dnf::MySqlConnectionPoolOptions&)>& mutate)
{
    dnf::MySqlConnectionPoolOptions options = ValidOptions();
    mutate(options);

    try
    {
        dnf::ValidateMySqlConnectionPoolOptions(options);
    }
    catch (const std::invalid_argument&)
    {
        return;
    }

    throw std::runtime_error("Invalid MySQL pool config was accepted");
}
} // namespace

int main()
{
    dnf::MySqlConnectionPoolOptions options = ValidOptions();
    dnf::ValidateMySqlConnectionPoolOptions(options);
    Check(options.maxSize == 4);
    Check(dnf::MYSQL_SAVE_MAX_TRANSACTION_ATTEMPTS == 3);
    Check(dnf::IsRetryableMySqlSaveError(
        boost::mysql::make_error_code(
            boost::mysql::common_server_errc::er_lock_deadlock)));
    Check(dnf::IsRetryableMySqlSaveError(
        boost::mysql::make_error_code(
            boost::mysql::common_server_errc::er_lock_wait_timeout)));
    Check(!dnf::IsRetryableMySqlSaveError(
        boost::mysql::make_error_code(
            boost::mysql::common_server_errc::er_dup_entry)));

    ExpectInvalid([](auto& value)
    {
        value.host.clear();
    });
    ExpectInvalid([](auto& value)
    {
        value.port = 0;
    });
    ExpectInvalid([](auto& value)
    {
        value.username.clear();
    });
    ExpectInvalid([](auto& value)
    {
        value.database.clear();
    });
    ExpectInvalid([](auto& value)
    {
        value.maxSize = 0;
    });
    ExpectInvalid([](auto& value)
    {
        value.maxSize = dnf::MAX_MYSQL_CONNECTION_POOL_SIZE + 1;
    });
    ExpectInvalid([](auto& value)
    {
        value.initialSize = value.maxSize + 1;
    });
    ExpectInvalid([](auto& value)
    {
        value.connectTimeout = std::chrono::milliseconds::zero();
    });
    ExpectInvalid([](auto& value)
    {
        value.acquireTimeout = std::chrono::milliseconds::zero();
    });
    ExpectInvalid([](auto& value)
    {
        value.queryTimeout = std::chrono::milliseconds::zero();
    });
    ExpectInvalid([](auto& value)
    {
        value.tlsMode = static_cast<dnf::MySqlTlsMode>(255);
    });
}
