#include "SqliteDatabase.h"

#include <sqlite3.h>

#include <cassert>
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
               "('players', 'player_skills');") == 2);
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
    TestEmptyPathIsRejected();

    std::cout << "All SQLite database tests passed.\n";
    return 0;
}
