#include "SqliteAccountPlayerRepository.h"

#include <sqlite3.h>

#include <cstdint>
#include <limits>

namespace dnf
{
namespace
{
class Statement
{
public:
    Statement(sqlite3* database, const char* sql)
        : database_(database)
    {
        if (sqlite3_prepare_v2(
                database_,
                sql,
                -1,
                &statement_,
                nullptr) != SQLITE_OK)
        {
            throw DatabaseError(sqlite3_errmsg(database_));
        }
    }

    ~Statement()
    {
        sqlite3_finalize(statement_);
    }

    sqlite3_stmt* Get() const
    {
        return statement_;
    }

    int Step() const
    {
        return sqlite3_step(statement_);
    }

private:
    sqlite3* database_;
    sqlite3_stmt* statement_ = nullptr;
};

void BindInt64(
    sqlite3* database,
    sqlite3_stmt* statement,
    int index,
    std::int64_t value)
{
    if (sqlite3_bind_int64(statement, index, value) != SQLITE_OK)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }
}

bool CanStoreId(std::uint64_t id)
{
    return id > 0 &&
           id <= static_cast<std::uint64_t>(
               std::numeric_limits<std::int64_t>::max());
}

void BindIds(
    sqlite3* database,
    sqlite3_stmt* statement,
    AccountId accountId,
    PlayerId playerId)
{
    BindInt64(
        database,
        statement,
        1,
        static_cast<std::int64_t>(accountId));
    BindInt64(
        database,
        statement,
        2,
        static_cast<std::int64_t>(playerId));
}
} // namespace

SqliteAccountPlayerRepository::SqliteAccountPlayerRepository(
    SqliteDatabase& database)
    : database_(database)
{
}

bool SqliteAccountPlayerRepository::LinkPlayer(
    AccountId accountId,
    PlayerId playerId)
{
    if (!CanStoreId(accountId) || !CanStoreId(playerId))
    {
        return false;
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "INSERT INTO account_players(account_id, player_id) "
        "VALUES(?, ?);");
    BindIds(database, statement.Get(), accountId, playerId);

    const int stepResult = statement.Step();
    if (stepResult == SQLITE_CONSTRAINT)
    {
        return false;
    }

    if (stepResult != SQLITE_DONE)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    return true;
}

bool SqliteAccountPlayerRepository::OwnsPlayer(
    AccountId accountId,
    PlayerId playerId)
{
    if (!CanStoreId(accountId) || !CanStoreId(playerId))
    {
        return false;
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "SELECT 1 FROM account_players "
        "WHERE account_id = ? AND player_id = ?;");
    BindIds(database, statement.Get(), accountId, playerId);

    const int stepResult = statement.Step();
    if (stepResult == SQLITE_ROW)
    {
        return true;
    }

    if (stepResult == SQLITE_DONE)
    {
        return false;
    }

    throw DatabaseError(sqlite3_errmsg(database));
}

std::vector<PlayerId> SqliteAccountPlayerRepository::FindPlayerIds(
    AccountId accountId)
{
    if (!CanStoreId(accountId))
    {
        return {};
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "SELECT player_id FROM account_players "
        "WHERE account_id = ? ORDER BY player_id;");
    BindInt64(
        database,
        statement.Get(),
        1,
        static_cast<std::int64_t>(accountId));

    std::vector<PlayerId> playerIds;
    while (true)
    {
        const int stepResult = statement.Step();
        if (stepResult == SQLITE_DONE)
        {
            return playerIds;
        }

        if (stepResult != SQLITE_ROW)
        {
            throw DatabaseError(sqlite3_errmsg(database));
        }

        const std::int64_t playerId =
            sqlite3_column_int64(statement.Get(), 0);
        if (playerId <= 0)
        {
            throw DatabaseError(
                "Invalid player ID in account_players table");
        }

        playerIds.push_back(static_cast<PlayerId>(playerId));
    }
}
} // namespace dnf
