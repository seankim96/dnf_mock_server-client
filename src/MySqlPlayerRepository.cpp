#include "MySqlPlayerRepository.h"

#include "DatabaseError.h"
#include "LoginValidator.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/cancel_after.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/common_server_errc.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/field_view.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/rows_view.hpp>
#include <boost/mysql/with_params.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <cstdint>
#include <future>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace dnf
{
bool IsRetryableMySqlSaveError(
    const boost::mysql::error_code& error)
{
    return error == boost::mysql::make_error_code(
                        boost::mysql::common_server_errc::
                            er_lock_deadlock) ||
           error == boost::mysql::make_error_code(
                        boost::mysql::common_server_errc::
                            er_lock_wait_timeout);
}

namespace
{
class MySqlOperationError final : public DatabaseError
{
public:
    MySqlOperationError(
        std::string_view operation,
        boost::mysql::error_code error)
        : DatabaseError(
              std::string(operation) + " failed: " + error.message()),
          error_(error)
    {
    }

    const boost::mysql::error_code& Code() const
    {
        return error_;
    }

private:
    boost::mysql::error_code error_;
};

[[noreturn]] void ThrowMySqlError(
    std::string_view operation,
    const boost::mysql::error_code& error)
{
    throw MySqlOperationError(operation, error);
}

template <typename ExecutionRequest>
boost::mysql::error_code Execute(
    boost::mysql::any_connection& connection,
    ExecutionRequest&& request,
    boost::mysql::results& results,
    std::chrono::milliseconds timeout)
{
    using Request = std::decay_t<ExecutionRequest>;

    std::future<boost::mysql::error_code> future =
        boost::asio::co_spawn(
            connection.get_executor(),
            [&connection,
             &results,
             request = Request(std::forward<ExecutionRequest>(request)),
             timeout]() mutable
                -> boost::asio::awaitable<boost::mysql::error_code>
            {
                boost::mysql::error_code error;
                co_await connection.async_execute(
                    std::move(request),
                    results,
                    boost::asio::cancel_after(
                        timeout,
                        boost::asio::redirect_error(
                            boost::asio::use_awaitable,
                            error)));
                co_return error;
            },
            boost::asio::use_future);

    try
    {
        return future.get();
    }
    catch (const boost::system::system_error& error)
    {
        return error.code();
    }
}

template <typename ExecutionRequest>
void ExecuteOrThrow(
    boost::mysql::any_connection& connection,
    ExecutionRequest&& request,
    boost::mysql::results& results,
    std::chrono::milliseconds timeout,
    std::string_view operation)
{
    const boost::mysql::error_code error = Execute(
        connection,
        std::forward<ExecutionRequest>(request),
        results,
        timeout);
    if (error)
    {
        ThrowMySqlError(operation, error);
    }
}

class Transaction
{
public:
    Transaction(
        boost::mysql::any_connection& connection,
        std::chrono::milliseconds queryTimeout)
        : connection_(connection),
          queryTimeout_(queryTimeout)
    {
        boost::mysql::results results;
        ExecuteOrThrow(
            connection_,
            boost::mysql::with_params("START TRANSACTION"),
            results,
            queryTimeout_,
            "Starting MySQL transaction");
    }

    ~Transaction()
    {
        if (committed_)
        {
            return;
        }

        try
        {
            boost::mysql::results results;
            static_cast<void>(Execute(
                connection_,
                boost::mysql::with_params("ROLLBACK"),
                results,
                queryTimeout_));
        }
        catch (...)
        {
        }
    }

    void Commit()
    {
        boost::mysql::results results;
        ExecuteOrThrow(
            connection_,
            boost::mysql::with_params("COMMIT"),
            results,
            queryTimeout_,
            "Committing MySQL transaction");
        committed_ = true;
    }

private:
    boost::mysql::any_connection& connection_;
    std::chrono::milliseconds queryTimeout_;
    bool committed_ = false;
};

bool CanStorePlayerId(std::uint64_t playerId)
{
    if (playerId == 0)
    {
        return false;
    }

    if constexpr (
        std::numeric_limits<PlayerId>::max() <
        std::numeric_limits<std::uint64_t>::max())
    {
        return playerId <= std::numeric_limits<PlayerId>::max();
    }

    return true;
}

std::uint64_t ReadUnsigned(
    boost::mysql::field_view field,
    std::uint64_t maximum)
{
    std::uint64_t value = 0;

    if (field.is_uint64())
    {
        value = field.as_uint64();
    }
    else if (field.is_int64() && field.as_int64() >= 0)
    {
        value = static_cast<std::uint64_t>(field.as_int64());
    }
    else
    {
        throw DatabaseError("Invalid unsigned integer in MySQL row");
    }

    if (value > maximum)
    {
        throw DatabaseError("MySQL integer is outside application range");
    }

    return value;
}

PlayerProfile ReadProfileRow(boost::mysql::row_view row)
{
    if (row.size() != 4 || !row.at(1).is_string())
    {
        throw DatabaseError("Invalid player row in MySQL database");
    }

    const std::uint64_t playerId = ReadUnsigned(
        row.at(0),
        std::numeric_limits<PlayerId>::max());
    if (!CanStorePlayerId(playerId))
    {
        throw DatabaseError("Invalid player ID in MySQL database");
    }

    const boost::mysql::string_view name = row.at(1).as_string();

    PlayerProfile profile;
    profile.playerId = static_cast<PlayerId>(playerId);
    profile.name.assign(name.data(), name.size());
    profile.level = static_cast<std::uint32_t>(ReadUnsigned(
        row.at(2),
        std::numeric_limits<std::uint32_t>::max()));
    profile.skillPoints = static_cast<std::uint32_t>(ReadUnsigned(
        row.at(3),
        std::numeric_limits<std::uint32_t>::max()));
    return profile;
}

std::vector<OwnedSkill> LoadSkills(
    MySqlConnectionPool& connectionPool,
    boost::mysql::any_connection& connection,
    PlayerId playerId)
{
    boost::mysql::results results;
    ExecuteOrThrow(
        connection,
        boost::mysql::with_params(
            "SELECT skill_id, skill_level "
            "FROM player_skills "
            "WHERE player_id = {} "
            "ORDER BY skill_id",
            playerId),
        results,
        connectionPool.QueryTimeout(),
        "Loading MySQL player skills");

    std::vector<OwnedSkill> skills;
    skills.reserve(results.rows().size());
    for (boost::mysql::row_view row : results.rows())
    {
        if (row.size() != 2)
        {
            throw DatabaseError("Invalid skill row in MySQL database");
        }

        const std::uint32_t skillId =
            static_cast<std::uint32_t>(ReadUnsigned(
                row.at(0),
                std::numeric_limits<std::uint32_t>::max()));
        const std::uint32_t skillLevel =
            static_cast<std::uint32_t>(ReadUnsigned(
                row.at(1),
                std::numeric_limits<std::uint32_t>::max()));
        skills.push_back({skillId, skillLevel});
    }

    return skills;
}

void ValidateLoadedProfile(
    MySqlConnectionPool& connectionPool,
    PlayerProfile& profile,
    boost::mysql::any_connection& connection)
{
    profile.skills = LoadSkills(
        connectionPool,
        connection,
        profile.playerId);
    if (!IsValidPlayerProfile(profile))
    {
        throw DatabaseError("Invalid player profile loaded from MySQL");
    }
}

bool SavePlayerOnce(
    MySqlConnectionPool& connectionPool,
    const PlayerProfile& profile)
{
    boost::mysql::pooled_connection connection =
        connectionPool.Acquire();
    const std::chrono::milliseconds queryTimeout =
        connectionPool.QueryTimeout();
    Transaction transaction(connection.get(), queryTimeout);

    boost::mysql::results existsResults;
    ExecuteOrThrow(
        connection.get(),
        boost::mysql::with_params(
            "SELECT 1 FROM players "
            "WHERE player_id = {} FOR UPDATE",
            profile.playerId),
        existsResults,
        queryTimeout,
        "Checking MySQL player existence");
    if (existsResults.rows().empty())
    {
        return false;
    }

    boost::mysql::results updateResults;
    ExecuteOrThrow(
        connection.get(),
        boost::mysql::with_params(
            "UPDATE players "
            "SET name = {}, level = {}, skill_points = {} "
            "WHERE player_id = {}",
            profile.name,
            profile.level,
            profile.skillPoints,
            profile.playerId),
        updateResults,
        queryTimeout,
        "Updating MySQL player");

    boost::mysql::results deleteResults;
    ExecuteOrThrow(
        connection.get(),
        boost::mysql::with_params(
            "DELETE FROM player_skills WHERE player_id = {}",
            profile.playerId),
        deleteResults,
        queryTimeout,
        "Deleting MySQL player skills");

    for (const OwnedSkill& skill : profile.skills)
    {
        boost::mysql::results insertResults;
        ExecuteOrThrow(
            connection.get(),
            boost::mysql::with_params(
                "INSERT INTO player_skills"
                "(player_id, skill_id, skill_level) "
                "VALUES({}, {}, {})",
                profile.playerId,
                skill.skillId,
                skill.level),
            insertResults,
            queryTimeout,
            "Inserting MySQL player skill");
    }

    transaction.Commit();
    return true;
}
} // namespace

MySqlPlayerRepository::MySqlPlayerRepository(
    MySqlConnectionPool& connectionPool)
    : connectionPool_(connectionPool)
{
}

std::optional<PlayerProfile> MySqlPlayerRepository::FindPlayer(
    PlayerId playerId)
{
    if (playerId == 0)
    {
        return std::nullopt;
    }

    boost::mysql::pooled_connection connection =
        connectionPool_.Acquire();
    boost::mysql::results results;
    ExecuteOrThrow(
        connection.get(),
        boost::mysql::with_params(
            "SELECT player_id, name, level, skill_points "
            "FROM players WHERE player_id = {}",
            playerId),
        results,
        connectionPool_.QueryTimeout(),
        "Finding MySQL player");

    if (results.rows().empty())
    {
        return std::nullopt;
    }

    if (results.rows().size() != 1)
    {
        throw DatabaseError("Duplicate player ID in MySQL database");
    }

    PlayerProfile profile = ReadProfileRow(results.rows().front());
    ValidateLoadedProfile(
        connectionPool_,
        profile,
        connection.get());
    return profile;
}

std::optional<PlayerProfile> MySqlPlayerRepository::FindPlayerByName(
    const std::string& playerName)
{
    if (LoginValidator().Validate(playerName).result != ValidPlayerName)
    {
        return std::nullopt;
    }

    boost::mysql::pooled_connection connection =
        connectionPool_.Acquire();
    boost::mysql::results results;
    ExecuteOrThrow(
        connection.get(),
        boost::mysql::with_params(
            "SELECT player_id, name, level, skill_points "
            "FROM players WHERE name = {}",
            playerName),
        results,
        connectionPool_.QueryTimeout(),
        "Finding MySQL player by name");

    if (results.rows().empty())
    {
        return std::nullopt;
    }

    if (results.rows().size() != 1)
    {
        throw DatabaseError("Duplicate player name in MySQL database");
    }

    PlayerProfile profile = ReadProfileRow(results.rows().front());
    ValidateLoadedProfile(
        connectionPool_,
        profile,
        connection.get());
    return profile;
}

std::optional<PlayerProfile> MySqlPlayerRepository::CreatePlayer(
    const std::string& playerName)
{
    if (LoginValidator().Validate(playerName).result != ValidPlayerName)
    {
        return std::nullopt;
    }

    boost::mysql::pooled_connection connection =
        connectionPool_.Acquire();
    boost::mysql::results results;
    const boost::mysql::error_code error = Execute(
        connection.get(),
        boost::mysql::with_params(
            "INSERT INTO players(name) VALUES({})",
            playerName),
        results,
        connectionPool_.QueryTimeout());

    if (error == boost::mysql::make_error_code(
            boost::mysql::common_server_errc::er_dup_entry))
    {
        return std::nullopt;
    }

    if (error)
    {
        ThrowMySqlError("Creating MySQL player", error);
    }

    const std::uint64_t playerId = results.last_insert_id();
    if (!CanStorePlayerId(playerId))
    {
        throw DatabaseError("MySQL returned an invalid player ID");
    }

    PlayerProfile profile;
    profile.playerId = static_cast<PlayerId>(playerId);
    profile.name = playerName;
    return profile;
}

bool MySqlPlayerRepository::SavePlayer(const PlayerProfile& profile)
{
    if (!IsValidPlayerProfile(profile))
    {
        return false;
    }

    for (std::size_t attempt = 1;
         attempt <= MYSQL_SAVE_MAX_TRANSACTION_ATTEMPTS;
         ++attempt)
    {
        try
        {
            return SavePlayerOnce(connectionPool_, profile);
        }
        catch (const MySqlOperationError& error)
        {
            if (!IsRetryableMySqlSaveError(error.Code()) ||
                attempt == MYSQL_SAVE_MAX_TRANSACTION_ATTEMPTS)
            {
                throw;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(10 * attempt));
        }
    }

    return false;
}
} // namespace dnf
