#include "SqliteAuthTicketStore.h"

#include <sqlite3.h>

#include <limits>
#include <string>

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

class Transaction
{
public:
    explicit Transaction(sqlite3* database)
        : database_(database)
    {
        Execute("BEGIN IMMEDIATE TRANSACTION;");
    }

    ~Transaction()
    {
        if (!committed_)
        {
            sqlite3_exec(database_, "ROLLBACK;", nullptr, nullptr, nullptr);
        }
    }

    void Commit()
    {
        Execute("COMMIT;");
        committed_ = true;
    }

private:
    void Execute(const char* sql)
    {
        if (sqlite3_exec(
                database_,
                sql,
                nullptr,
                nullptr,
                nullptr) != SQLITE_OK)
        {
            throw DatabaseError(sqlite3_errmsg(database_));
        }
    }

    sqlite3* database_;
    bool committed_ = false;
};

void BindText(
    sqlite3* database,
    sqlite3_stmt* statement,
    int index,
    const std::string& value)
{
    if (sqlite3_bind_text(
            statement,
            index,
            value.c_str(),
            static_cast<int>(value.size()),
            SQLITE_TRANSIENT) != SQLITE_OK)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }
}

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

bool CanStoreContext(const AuthContext& context)
{
    constexpr std::uint64_t MAX_DATABASE_ID =
        static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max());

    return IsValidAuthContext(context) &&
           context.accountId <= MAX_DATABASE_ID &&
           context.playerId <= MAX_DATABASE_ID;
}

void DeleteTicket(
    sqlite3* database,
    const std::string& ticket)
{
    Statement statement(
        database,
        "DELETE FROM auth_tickets WHERE ticket = ?;");
    BindText(database, statement.Get(), 1, ticket);

    if (statement.Step() != SQLITE_DONE)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }
}
} // namespace

SqliteAuthTicketStore::SqliteAuthTicketStore(SqliteDatabase& database)
    : database_(database)
{
}

bool SqliteAuthTicketStore::IssueTicket(
    const std::string& ticket,
    const AuthContext& context,
    std::int64_t expiresAtUnix)
{
    if (!IsValidAuthTicket(ticket) ||
        !CanStoreContext(context) ||
        expiresAtUnix <= 0)
    {
        return false;
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "INSERT INTO auth_tickets("
        "ticket, account_id, player_id, expires_at_unix) "
        "VALUES(?, ?, ?, ?);");
    BindText(database, statement.Get(), 1, ticket);
    BindInt64(
        database,
        statement.Get(),
        2,
        static_cast<std::int64_t>(context.accountId));
    BindInt64(
        database,
        statement.Get(),
        3,
        static_cast<std::int64_t>(context.playerId));
    BindInt64(database, statement.Get(), 4, expiresAtUnix);

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

std::optional<AuthContext> SqliteAuthTicketStore::ConsumeTicket(
    const std::string& ticket,
    std::int64_t nowUnix)
{
    if (!IsValidAuthTicket(ticket) || nowUnix < 0)
    {
        return std::nullopt;
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Transaction transaction(database);

    Statement selectStatement(
        database,
        "SELECT account_id, player_id, expires_at_unix "
        "FROM auth_tickets WHERE ticket = ?;");
    BindText(database, selectStatement.Get(), 1, ticket);

    const int stepResult = selectStatement.Step();
    if (stepResult == SQLITE_DONE)
    {
        transaction.Commit();
        return std::nullopt;
    }

    if (stepResult != SQLITE_ROW)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    const std::int64_t accountId =
        sqlite3_column_int64(selectStatement.Get(), 0);
    const std::int64_t playerId =
        sqlite3_column_int64(selectStatement.Get(), 1);
    const std::int64_t expiresAtUnix =
        sqlite3_column_int64(selectStatement.Get(), 2);

    DeleteTicket(database, ticket);
    transaction.Commit();

    if (expiresAtUnix <= nowUnix || accountId <= 0 || playerId <= 0)
    {
        return std::nullopt;
    }

    return AuthContext{
        static_cast<AccountId>(accountId),
        static_cast<PlayerId>(playerId)};
}
} // namespace dnf
