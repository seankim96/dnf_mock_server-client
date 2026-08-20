#include "SqlitePlayerRepository.h"

#include "LoginValidator.h"

#include <sqlite3.h>

#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

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

    void Reset() const
    {
        if (sqlite3_reset(statement_) != SQLITE_OK ||
            sqlite3_clear_bindings(statement_) != SQLITE_OK)
        {
            throw DatabaseError(sqlite3_errmsg(database_));
        }
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

void ExpectDone(sqlite3* database, const Statement& statement)
{
    if (statement.Step() != SQLITE_DONE)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }
}

bool CanStorePlayerId(PlayerId playerId)
{
    return playerId > 0 &&
           playerId <= static_cast<PlayerId>(
               std::numeric_limits<std::int64_t>::max());
}

std::int64_t ToDatabasePlayerId(PlayerId playerId)
{
    return static_cast<std::int64_t>(playerId);
}

std::uint32_t ReadUint32(
    sqlite3* database,
    sqlite3_stmt* statement,
    int column)
{
    const std::int64_t value = sqlite3_column_int64(statement, column);
    if (value < 0 ||
        value > std::numeric_limits<std::uint32_t>::max())
    {
        throw DatabaseError("SQLite integer is outside uint32 range");
    }

    return static_cast<std::uint32_t>(value);
}

PlayerProfile ReadProfileRow(
    sqlite3* database,
    sqlite3_stmt* statement)
{
    const std::int64_t playerId = sqlite3_column_int64(statement, 0);
    const unsigned char* nameText = sqlite3_column_text(statement, 1);

    if (playerId <= 0 || nameText == nullptr)
    {
        throw DatabaseError("Invalid player row in SQLite database");
    }

    PlayerProfile profile;
    profile.playerId = static_cast<PlayerId>(playerId);
    profile.name = reinterpret_cast<const char*>(nameText);
    profile.level = ReadUint32(database, statement, 2);
    profile.skillPoints = ReadUint32(database, statement, 3);
    return profile;
}

std::vector<OwnedSkill> LoadSkills(
    sqlite3* database,
    PlayerId playerId)
{
    Statement statement(
        database,
        "SELECT skill_id, skill_level "
        "FROM player_skills "
        "WHERE player_id = ? "
        "ORDER BY skill_id;");
    BindInt64(
        database,
        statement.Get(),
        1,
        ToDatabasePlayerId(playerId));

    std::vector<OwnedSkill> skills;
    while (true)
    {
        const int stepResult = statement.Step();
        if (stepResult == SQLITE_DONE)
        {
            return skills;
        }

        if (stepResult != SQLITE_ROW)
        {
            throw DatabaseError(sqlite3_errmsg(database));
        }

        const std::uint32_t skillId =
            ReadUint32(database, statement.Get(), 0);
        const std::uint32_t skillLevel =
            ReadUint32(database, statement.Get(), 1);
        skills.push_back({skillId, skillLevel});
    }
}

void ValidateLoadedProfile(PlayerProfile& profile, sqlite3* database)
{
    profile.skills = LoadSkills(database, profile.playerId);
    if (!IsValidPlayerProfile(profile))
    {
        throw DatabaseError("Invalid player profile loaded from SQLite");
    }
}
} // namespace

SqlitePlayerRepository::SqlitePlayerRepository(SqliteDatabase& database)
    : database_(database)
{
}

std::optional<PlayerProfile> SqlitePlayerRepository::FindPlayer(
    PlayerId playerId)
{
    if (!CanStorePlayerId(playerId))
    {
        return std::nullopt;
    }

    std::lock_guard lock(mutex_);
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "SELECT player_id, name, level, skill_points "
        "FROM players WHERE player_id = ?;");
    BindInt64(
        database,
        statement.Get(),
        1,
        ToDatabasePlayerId(playerId));

    const int stepResult = statement.Step();
    if (stepResult == SQLITE_DONE)
    {
        return std::nullopt;
    }

    if (stepResult != SQLITE_ROW)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    PlayerProfile profile = ReadProfileRow(database, statement.Get());
    ValidateLoadedProfile(profile, database);
    return profile;
}

std::optional<PlayerProfile> SqlitePlayerRepository::FindPlayerByName(
    const std::string& playerName)
{
    if (LoginValidator().Validate(playerName).result != LoginSuccess)
    {
        return std::nullopt;
    }

    std::lock_guard lock(mutex_);
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "SELECT player_id, name, level, skill_points "
        "FROM players WHERE name = ?;");
    BindText(database, statement.Get(), 1, playerName);

    const int stepResult = statement.Step();
    if (stepResult == SQLITE_DONE)
    {
        return std::nullopt;
    }

    if (stepResult != SQLITE_ROW)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    PlayerProfile profile = ReadProfileRow(database, statement.Get());
    ValidateLoadedProfile(profile, database);
    return profile;
}

std::optional<PlayerProfile> SqlitePlayerRepository::CreatePlayer(
    const std::string& playerName)
{
    if (LoginValidator().Validate(playerName).result != LoginSuccess)
    {
        return std::nullopt;
    }

    std::lock_guard lock(mutex_);
    sqlite3* database = database_.Handle();
    Statement statement(
        database,
        "INSERT INTO players(name) VALUES(?);");
    BindText(database, statement.Get(), 1, playerName);

    const int stepResult = statement.Step();
    if (stepResult == SQLITE_CONSTRAINT)
    {
        return std::nullopt;
    }

    if (stepResult != SQLITE_DONE)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    const std::int64_t playerId = sqlite3_last_insert_rowid(database);
    if (playerId <= 0)
    {
        throw DatabaseError("SQLite returned an invalid player ID");
    }

    PlayerProfile profile;
    profile.playerId = static_cast<PlayerId>(playerId);
    profile.name = playerName;
    return profile;
}

bool SqlitePlayerRepository::SavePlayer(const PlayerProfile& profile)
{
    if (!CanStorePlayerId(profile.playerId) ||
        !IsValidPlayerProfile(profile))
    {
        return false;
    }

    std::lock_guard lock(mutex_);
    sqlite3* database = database_.Handle();
    Transaction transaction(database);

    Statement existsStatement(
        database,
        "SELECT 1 FROM players WHERE player_id = ?;");
    BindInt64(
        database,
        existsStatement.Get(),
        1,
        ToDatabasePlayerId(profile.playerId));
    const int existsResult = existsStatement.Step();
    if (existsResult == SQLITE_DONE)
    {
        return false;
    }

    if (existsResult != SQLITE_ROW)
    {
        throw DatabaseError(sqlite3_errmsg(database));
    }

    Statement updateStatement(
        database,
        "UPDATE players "
        "SET name = ?, level = ?, skill_points = ? "
        "WHERE player_id = ?;");
    BindText(database, updateStatement.Get(), 1, profile.name);
    BindInt64(database, updateStatement.Get(), 2, profile.level);
    BindInt64(database, updateStatement.Get(), 3, profile.skillPoints);
    BindInt64(
        database,
        updateStatement.Get(),
        4,
        ToDatabasePlayerId(profile.playerId));
    ExpectDone(database, updateStatement);

    Statement deleteSkillsStatement(
        database,
        "DELETE FROM player_skills WHERE player_id = ?;");
    BindInt64(
        database,
        deleteSkillsStatement.Get(),
        1,
        ToDatabasePlayerId(profile.playerId));
    ExpectDone(database, deleteSkillsStatement);

    Statement insertSkillStatement(
        database,
        "INSERT INTO player_skills(player_id, skill_id, skill_level) "
        "VALUES(?, ?, ?);");
    for (const OwnedSkill& skill : profile.skills)
    {
        insertSkillStatement.Reset();
        BindInt64(
            database,
            insertSkillStatement.Get(),
            1,
            ToDatabasePlayerId(profile.playerId));
        BindInt64(database, insertSkillStatement.Get(), 2, skill.skillId);
        BindInt64(database, insertSkillStatement.Get(), 3, skill.level);
        ExpectDone(database, insertSkillStatement);
    }

    transaction.Commit();
    return true;
}
} // namespace dnf
