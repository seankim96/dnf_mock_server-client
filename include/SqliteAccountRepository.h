#pragma once

#include "AccountRepository.h"
#include "SqliteDatabase.h"

namespace dnf
{
class SqliteAccountRepository final : public AccountRepository
{
public:
    explicit SqliteAccountRepository(SqliteDatabase& database);

    std::optional<Account> FindAccount(
        AccountId accountId) override;
    std::optional<Account> FindAccountByLoginId(
        const std::string& loginId) override;
    std::optional<Account> CreateAccount(
        const std::string& loginId,
        const std::string& encodedPasswordHash) override;

private:
    SqliteDatabase& database_;
};
} // namespace dnf
