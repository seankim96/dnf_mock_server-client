#pragma once

#include "PlayerRepository.h"
#include "SqliteDatabase.h"

namespace dnf
{
class SqlitePlayerRepository final : public PlayerRepository
{
public:
    explicit SqlitePlayerRepository(SqliteDatabase& database);

    std::optional<PlayerProfile> FindPlayer(
        PlayerId playerId) override;
    std::optional<PlayerProfile> FindPlayerByName(
        const std::string& playerName) override;
    std::optional<PlayerProfile> CreatePlayer(
        const std::string& playerName) override;
    bool SavePlayer(const PlayerProfile& profile) override;

private:
    SqliteDatabase& database_;
};
} // namespace dnf
