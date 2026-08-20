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

void SqliteDatabase::InitializeSchema()
{
    Execute(database_, "PRAGMA foreign_keys = ON;");

    const int schemaVersion = ReadSchemaVersion(database_);
    if (schemaVersion > SQLITE_DATABASE_SCHEMA_VERSION)
    {
        throw DatabaseError("SQLite database schema is newer than the server");
    }

    if (schemaVersion == SQLITE_DATABASE_SCHEMA_VERSION)
    {
        return;
    }

    Execute(database_, "BEGIN IMMEDIATE TRANSACTION;");

    try
    {
        Execute(
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
        Execute(database_, "COMMIT;");
    }
    catch (...)
    {
        sqlite3_exec(
            database_,
            "ROLLBACK;",
            nullptr,
            nullptr,
            nullptr);
        throw;
    }
}
} // namespace dnf
