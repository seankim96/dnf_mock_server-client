#pragma once

#include "AccountPlayerRepository.h"
#include "SqliteDatabase.h"

namespace dnf
{
class SqliteAccountPlayerRepository final
    : public AccountPlayerRepository
{
public:
    explicit SqliteAccountPlayerRepository(SqliteDatabase& database);

    bool LinkPlayer(
        AccountId accountId,
        PlayerId playerId) override;
    bool OwnsPlayer(
        AccountId accountId,
        PlayerId playerId) override;
    std::vector<PlayerId> FindPlayerIds(
        AccountId accountId) override;

private:
    SqliteDatabase& database_;
};
} // namespace dnf
