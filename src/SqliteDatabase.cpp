#include "SqliteDatabase.h"

#include <sqlite3.h>

#include <string>

namespace dnf
{
namespace
{
void Execute(sqlite3* database, const char* sql)
{
    char* errorMessage = nullptr;
    const int result = sqlite3_exec(
        database,
        sql,
        nullptr,
        nullptr,
        &errorMessage);

    if (result == SQLITE_OK)
    {
        return;
    }

    const std::string message = errorMessage != nullptr
        ? errorMessage
        : sqlite3_errmsg(database);
    sqlite3_free(errorMessage);
    throw DatabaseError(message);
}

int ReadSchemaVersion(sqlite3* database)
{
    sqlite3_stmt* statement = nullptr;
    if (sqlite3_prepare_v2(
            database,
            "PRAGMA user_version;",
            -1,
            &statement,
            nullptr) != SQLITE_OK)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    const int stepResult = sqlite3_step(statement);
    if (stepResult != SQLITE_ROW)
    {
        const std::string message = sqlite3_errmsg(database);
        sqlite3_finalize(statement);
        throw DatabaseError(message);
    }

    const int version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
    return version;
}

void RunMigration(sqlite3* database, const char* sql)
{
    Execute(database, "BEGIN IMMEDIATE TRANSACTION;");

    try
    {
        Execute(database, sql);
        Execute(database, "COMMIT;");
    }
    catch (...)
    {
        sqlite3_exec(database, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw;
    }
}
} // namespace

SqliteDatabase::SqliteDatabase(const std::string& databasePath)
{
    if (databasePath.empty())
    {
        throw std::invalid_argument("Database path must not be empty");
    }

    const int openResult = sqlite3_open_v2(
        databasePath.c_str(),
        &database_,
        SQLITE_OPEN_READWRITE |
            SQLITE_OPEN_CREATE |
            SQLITE_OPEN_FULLMUTEX,
        nullptr);

    if (openResult != SQLITE_OK)
    {
        const std::string message = database_ != nullptr
            ? sqlite3_errmsg(database_)
            : "Failed to open SQLite database";
        sqlite3_close_v2(database_);
        database_ = nullptr;
        throw DatabaseError(message);
    }

    if (sqlite3_busy_timeout(database_, 5000) != SQLITE_OK)
    {
        const std::string message = sqlite3_errmsg(database_);
        sqlite3_close_v2(database_);
        database_ = nullptr;
        throw DatabaseError(message);
    }

    try
    {
        InitializeSchema();
    }
    catch (...)
    {
        sqlite3_close_v2(database_);
        database_ = nullptr;
        throw;
    }
}

SqliteDatabase::~SqliteDatabase()
{
    sqlite3_close_v2(database_);
}

sqlite3* SqliteDatabase::Handle() const
{
    return database_;
}

std::mutex& SqliteDatabase::ConnectionMutex()
{
    return connectionMutex_;
}

void SqliteDatabase::InitializeSchema()
{
    Execute(database_, "PRAGMA foreign_keys = ON;");

    int schemaVersion = ReadSchemaVersion(database_);
    if (schemaVersion > SQLITE_DATABASE_SCHEMA_VERSION)
    {
        throw DatabaseError("SQLite database schema is newer than the server");
    }

    if (schemaVersion < 1)
    {
        RunMigration(
            database_,
            "CREATE TABLE players ("
            "player_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "name TEXT NOT NULL UNIQUE "
                "CHECK(length(name) BETWEEN 1 AND 16),"
            "level INTEGER NOT NULL DEFAULT 1 CHECK(level >= 1),"
            "skill_points INTEGER NOT NULL DEFAULT 0 "
                "CHECK(skill_points >= 0)"
            ");"
            "CREATE TABLE player_skills ("
            "player_id INTEGER NOT NULL,"
            "skill_id INTEGER NOT NULL CHECK(skill_id > 0),"
            "skill_level INTEGER NOT NULL DEFAULT 1 "
                "CHECK(skill_level >= 1),"
            "PRIMARY KEY(player_id, skill_id),"
            "FOREIGN KEY(player_id) REFERENCES players(player_id) "
                "ON DELETE CASCADE"
            ");"
            "PRAGMA user_version = 1;");
        schemaVersion = 1;
    }

    if (schemaVersion < 2)
    {
        RunMigration(
            database_,
            "CREATE TABLE auth_tickets ("
            "ticket TEXT PRIMARY KEY "
                "CHECK(length(ticket) BETWEEN 1 AND 256),"
            "account_id INTEGER NOT NULL CHECK(account_id > 0),"
            "player_id INTEGER NOT NULL CHECK(player_id > 0),"
            "expires_at_unix INTEGER NOT NULL "
                "CHECK(expires_at_unix > 0),"
            "FOREIGN KEY(player_id) REFERENCES players(player_id) "
                "ON DELETE CASCADE"
            ");"
            "CREATE INDEX auth_tickets_expiry_index "
            "ON auth_tickets(expires_at_unix);"
            "PRAGMA user_version = 2;");
        schemaVersion = 2;
    }

    if (schemaVersion < 3)
    {
        RunMigration(
            database_,
            "CREATE TABLE accounts ("
            "account_id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "login_id TEXT NOT NULL COLLATE NOCASE UNIQUE "
                "CHECK(length(login_id) BETWEEN 4 AND 32),"
            "password_hash TEXT NOT NULL "
                "CHECK(length(password_hash) BETWEEN 1 AND 256)"
            ");"
            "PRAGMA user_version = 3;");
        schemaVersion = 3;
    }

    if (schemaVersion < 4)
    {
        RunMigration(
            database_,
            "CREATE TABLE account_players ("
            "account_id INTEGER NOT NULL,"
            "player_id INTEGER NOT NULL UNIQUE,"
            "PRIMARY KEY(account_id, player_id),"
            "FOREIGN KEY(account_id) REFERENCES accounts(account_id) "
                "ON DELETE CASCADE,"
            "FOREIGN KEY(player_id) REFERENCES players(player_id) "
                "ON DELETE CASCADE"
            ");"
            "PRAGMA user_version = 4;");
    }
}
} // namespace dnf
