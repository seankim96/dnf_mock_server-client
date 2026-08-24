#pragma once

#include "MySqlConnectionPool.h"
#include "PlayerRepository.h"

#include <boost/mysql/error_code.hpp>

#include <cstddef>

namespace dnf
{
constexpr std::size_t MYSQL_SAVE_MAX_TRANSACTION_ATTEMPTS = 3;

bool IsRetryableMySqlSaveError(
    const boost::mysql::error_code& error);

class MySqlPlayerRepository final : public PlayerRepository
{
public:
    explicit MySqlPlayerRepository(MySqlConnectionPool& connectionPool);

    std::optional<PlayerProfile> FindPlayer(
        PlayerId playerId) override;
    std::optional<PlayerProfile> FindPlayerByName(
        const std::string& playerName) override;
    std::optional<PlayerProfile> CreatePlayer(
        const std::string& playerName) override;
    bool SavePlayer(const PlayerProfile& profile) override;

private:
    MySqlConnectionPool& connectionPool_;
};
} // namespace dnf
