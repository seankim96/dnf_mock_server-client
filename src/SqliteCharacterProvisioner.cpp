#include "SqliteCharacterProvisioner.h"

#include "LoginValidator.h"

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
            sqlite3_exec(
                database_,
                "ROLLBACK;",
                nullptr,
                nullptr,
                nullptr);
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

bool CanStoreAccountId(AccountId accountId)
{
    return accountId > 0 &&
           accountId <= static_cast<AccountId>(
               std::numeric_limits<std::int64_t>::max());
}
} // namespace

SqliteCharacterProvisioner::SqliteCharacterProvisioner(
    SqliteDatabase& database)
    : database_(database)
{
}

CharacterProvisioningResult
SqliteCharacterProvisioner::CreateOwnedPlayer(
    AccountId accountId,
    const std::string& playerName)
{
    if (!CanStoreAccountId(accountId) ||
        LoginValidator().Validate(playerName).result != ValidPlayerName)
    {
        return {CharacterProvisioningStatus::InvalidInput, 0};
    }

    std::lock_guard lock(database_.ConnectionMutex());
    sqlite3* database = database_.Handle();
    Transaction transaction(database);

    {
        Statement accountStatement(
            database,
            "SELECT 1 FROM accounts WHERE account_id = ?;");
        BindInt64(
            database,
            accountStatement.Get(),
            1,
            static_cast<std::int64_t>(accountId));

        const int accountResult = accountStatement.Step();
        if (accountResult == SQLITE_DONE)
        {
            return {CharacterProvisioningStatus::AccountNotFound, 0};
        }

        if (accountResult != SQLITE_ROW)
        {
            throw DatabaseError(sqlite3_errmsg(database));
        }
    }

    Statement playerStatement(
        database,
        "INSERT INTO players(name) VALUES(?);");
    BindText(database, playerStatement.Get(), 1, playerName);

    const int playerResult = playerStatement.Step();
    if (playerResult == SQLITE_CONSTRAINT)
    {
        return {
            CharacterProvisioningStatus::PlayerNameAlreadyExists,
            0};
    }

    if (playerResult != SQLITE_DONE)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    const std::int64_t playerId =
        sqlite3_last_insert_rowid(database);
    if (playerId <= 0)
    {
        throw DatabaseError("SQLite returned an invalid player ID");
    }

    Statement ownershipStatement(
        database,
        "INSERT INTO account_players(account_id, player_id) "
        "VALUES(?, ?);");
    BindInt64(
        database,
        ownershipStatement.Get(),
        1,
        static_cast<std::int64_t>(accountId));
    BindInt64(
        database,
        ownershipStatement.Get(),
        2,
        playerId);

    if (ownershipStatement.Step() != SQLITE_DONE)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    transaction.Commit();
    return {
        CharacterProvisioningStatus::Success,
        static_cast<PlayerId>(playerId)};
}
} // namespace dnf
