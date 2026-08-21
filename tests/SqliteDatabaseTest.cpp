#include "SqliteDatabase.h"

#include <sqlite3.h>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
int QueryInt(sqlite3* database, const char* sql)
{
    sqlite3_stmt* statement = nullptr;
    assert(sqlite3_prepare_v2(
               database,
               sql,
               -1,
               &statement,
               nullptr) == SQLITE_OK);
    assert(sqlite3_step(statement) == SQLITE_ROW);

    const int value = sqlite3_column_int(statement, 0);
    assert(sqlite3_finalize(statement) == SQLITE_OK);
    return value;
}

int Execute(sqlite3* database, const char* sql)
{
    return sqlite3_exec(database, sql, nullptr, nullptr, nullptr);
}

void TestSchemaIsCreated()
{
    dnf::SqliteDatabase database(":memory:");
    sqlite3* handle = database.Handle();

    assert(handle != nullptr);
    assert(QueryInt(handle, "PRAGMA user_version;") ==
           dnf::SQLITE_DATABASE_SCHEMA_VERSION);
    assert(QueryInt(handle, "PRAGMA foreign_keys;") == 1);
    assert(QueryInt(
               handle,
               "SELECT COUNT(*) FROM sqlite_master "
               "WHERE type = 'table' AND name IN "
               "('players', 'player_skills', 'auth_tickets');") == 3);
}

void TestVersionOneDatabaseIsMigrated()
{
    const auto uniqueSuffix =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path databasePath =
        std::filesystem::temp_directory_path() /
        ("dnf_schema_migration_" +
         std::to_string(uniqueSuffix) +
         ".db");

    struct TemporaryFile
    {
        std::filesystem::path path;

        ~TemporaryFile()
        {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    } temporaryFile{databasePath};

    sqlite3* oldDatabase = nullptr;
    assert(sqlite3_open(databasePath.string().c_str(), &oldDatabase) ==
           SQLITE_OK);
    assert(Execute(
               oldDatabase,
               "CREATE TABLE players ("
               "player_id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "name TEXT NOT NULL UNIQUE,"
               "level INTEGER NOT NULL DEFAULT 1,"
               "skill_points INTEGER NOT NULL DEFAULT 0"
               ");"
               "CREATE TABLE player_skills ("
               "player_id INTEGER NOT NULL,"
               "skill_id INTEGER NOT NULL,"
               "skill_level INTEGER NOT NULL DEFAULT 1,"
               "PRIMARY KEY(player_id, skill_id),"
               "FOREIGN KEY(player_id) REFERENCES players(player_id)"
               ");"
               "INSERT INTO players(name) VALUES('ExistingPlayer');"
               "PRAGMA user_version = 1;") == SQLITE_OK);
    assert(sqlite3_close(oldDatabase) == SQLITE_OK);

    dnf::SqliteDatabase database(databasePath.string());
    assert(QueryInt(database.Handle(), "PRAGMA user_version;") ==
           dnf::SQLITE_DATABASE_SCHEMA_VERSION);
    assert(QueryInt(
               database.Handle(),
               "SELECT COUNT(*) FROM auth_tickets;") == 0);
    assert(QueryInt(
               database.Handle(),
               "SELECT COUNT(*) FROM players "
               "WHERE name = 'ExistingPlayer';") == 1);
}

void TestSchemaConstraints()
{
    dnf::SqliteDatabase database(":memory:");
    sqlite3* handle = database.Handle();

    assert(Execute(
               handle,
               "INSERT INTO players(name, level, skill_points) "
               "VALUES('Player_1', 10, 3);") == SQLITE_OK);
    assert(Execute(
               handle,
               "INSERT INTO player_skills(player_id, skill_id, skill_level) "
               "VALUES(1, 1001, 2);") == SQLITE_OK);

    assert(Execute(
               handle,
               "INSERT INTO players(name) VALUES('Player_1');") ==
           SQLITE_CONSTRAINT);
    assert(Execute(
               handle,
               "INSERT INTO player_skills(player_id, skill_id) "
               "VALUES(999, 1001);") == SQLITE_CONSTRAINT);

    assert(Execute(handle, "DELETE FROM players WHERE player_id = 1;") ==
           SQLITE_OK);
    assert(QueryInt(handle, "SELECT COUNT(*) FROM player_skills;") == 0);
}

void TestEmptyPathIsRejected()
{
    bool rejected = false;
    try
    {
        dnf::SqliteDatabase database("");
    }
    catch (const std::invalid_argument&)
    {
        rejected = true;
    }

    assert(rejected);
}
} // namespace

int main()
{
    TestSchemaIsCreated();
    TestSchemaConstraints();
    TestVersionOneDatabaseIsMigrated();
    TestEmptyPathIsRejected();

    std::cout << "All SQLite database tests passed.\n";
    return 0;
}
