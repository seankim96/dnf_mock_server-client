#pragma once

#include "AccountPlayerRepository.h"
#include "AccountRepository.h"
#include "DatabaseExecutor.h"
#include "PlayerRepository.h"

#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace dnf
{
struct CharacterSummary
{
    PlayerId playerId = 0;
    std::string name;
    std::uint32_t level = 1;
};

enum class CharacterListStatus
{
    Success,
    AccountNotFound,
    ServiceBusy,
    ServiceError
};

struct CharacterListResult
{
    CharacterListStatus status = CharacterListStatus::AccountNotFound;
    std::vector<CharacterSummary> characters;
};

class CharacterListService
{
public:
    using CompletionHandler = std::function<void(CharacterListResult)>;

    CharacterListService(
        boost::asio::io_context& ioContext,
        DatabaseExecutor& databaseExecutor,
        AccountRepository& accountRepository,
        AccountPlayerRepository& accountPlayerRepository,
        PlayerRepository& playerRepository);

    void LoadCharacters(
        AccountId authenticatedAccountId,
        CompletionHandler completionHandler);

private:
    boost::asio::io_context& ioContext_;
    DatabaseExecutor& databaseExecutor_;
    AccountRepository& accountRepository_;
    AccountPlayerRepository& accountPlayerRepository_;
    PlayerRepository& playerRepository_;
};
} // namespace dnf
