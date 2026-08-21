#pragma once

#include "AccountId.h"
#include "PlayerId.h"
#include "SqliteDatabase.h"

#include <string>

namespace dnf
{
enum class CharacterProvisioningStatus
{
    Success,
    InvalidInput,
    AccountNotFound,
    PlayerNameAlreadyExists
};

struct CharacterProvisioningResult
{
    CharacterProvisioningStatus status =
        CharacterProvisioningStatus::InvalidInput;
    PlayerId playerId = 0;
};

class SqliteCharacterProvisioner
{
public:
    explicit SqliteCharacterProvisioner(SqliteDatabase& database);

    CharacterProvisioningResult CreateOwnedPlayer(
        AccountId accountId,
        const std::string& playerName);

private:
    SqliteDatabase& database_;
};
} // namespace dnf
