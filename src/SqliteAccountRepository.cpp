#include "SqliteAccountRepository.h"

#include <sqlite3.h>

#include <cstdint>
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

bool CanStoreAccountId(AccountId accountId)
{
    return accountId > 0 &&
           accountId <= static_cast<AccountId>(
               std::numeric_limits<std::int64_t>::max());
}

Account ReadAccountRow(sqlite3_stmt* statement)
{
    const std::int64_t accountId = sqlite3_column_int64(statement, 0);
    const unsigned char* loginId = sqlite3_column_text(statement, 1);
    const unsigned char* passwordHash = sqlite3_column_text(statement, 2);

    if (accountId <= 0 || loginId == nullptr || passwordHash == nullptr)
    {
        throw DatabaseError("Invalid account row in SQLite database");
    }

    Account account;
    account.accountId = static_cast<AccountId>(accountId);
    account.loginId = reinterpret_cast<const char*>(loginId);
    account.encodedPasswordHash =
        reinterpret_cast<const char*>(passwordHash);

    if (!IsValidAccount(account))
    {
        throw DatabaseError("Invalid account data in SQLite database");
    }

    return account;
}

std::optional<Account> ReadSingleAccount(
    sqlite3* database,
    Statement& statement)
{
    const int stepResult = statement.Step();
    if (stepResult == SQLITE_DONE)
    {
        return std::nullopt;
    }

    if (stepResult != SQLITE_ROW)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    return ReadAccountRow(statement.Get());
}
} // namespace

SqliteAccountRepository::SqliteAccountRepository(SqliteDatabase& database)
    : database_(database)
{
}

std::optional<Account> SqliteAccountRepository::FindAccount(
    AccountId accountId)
{
    if (!CanStoreAccountId(accountId))
    {
        return std::nullopt;
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "SELECT account_id, login_id, password_hash "
        "FROM accounts WHERE account_id = ?;");
    BindInt64(
        database,
        statement.Get(),
        1,
        static_cast<std::int64_t>(accountId));
    return ReadSingleAccount(database, statement);
}

std::optional<Account> SqliteAccountRepository::FindAccountByLoginId(
    const std::string& loginId)
{
    if (!IsValidAccountLoginId(loginId))
    {
        return std::nullopt;
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "SELECT account_id, login_id, password_hash "
        "FROM accounts WHERE login_id = ?;");
    BindText(database, statement.Get(), 1, loginId);
    return ReadSingleAccount(database, statement);
}

std::optional<Account> SqliteAccountRepository::CreateAccount(
    const std::string& loginId,
    const std::string& encodedPasswordHash)
{
    if (!IsValidAccountLoginId(loginId) ||
        !IsValidEncodedPasswordHash(encodedPasswordHash))
    {
        return std::nullopt;
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "INSERT INTO accounts(login_id, password_hash) VALUES(?, ?);");
    BindText(database, statement.Get(), 1, loginId);
    BindText(database, statement.Get(), 2, encodedPasswordHash);

    const int stepResult = statement.Step();
    if (stepResult == SQLITE_CONSTRAINT)
    {
        return std::nullopt;
    }

    if (stepResult != SQLITE_DONE)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    const std::int64_t accountId = sqlite3_last_insert_rowid(database);
    if (accountId <= 0)
    {
        throw DatabaseError("SQLite returned an invalid account ID");
    }

    return Account{
        static_cast<AccountId>(accountId),
        loginId,
        encodedPasswordHash};
}
} // namespace dnf
