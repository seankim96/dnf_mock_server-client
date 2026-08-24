#include "CharacterListService.h"

#include <boost/asio/post.hpp>

#include <stdexcept>
#include <utility>

namespace dnf
{
namespace
{
void PostCompletion(
    boost::asio::io_context& ioContext,
    CharacterListService::CompletionHandler completionHandler,
    CharacterListResult result)
{
    boost::asio::post(
        ioContext,
        [completionHandler = std::move(completionHandler),
         result = std::move(result)]() mutable
        {
            completionHandler(std::move(result));
        });
}
} // namespace

CharacterListService::CharacterListService(
    boost::asio::io_context& ioContext,
    DatabaseExecutor& databaseExecutor,
    AccountRepository& accountRepository,
    AccountPlayerRepository& accountPlayerRepository,
    PlayerRepository& playerRepository)
    : ioContext_(ioContext),
      databaseExecutor_(databaseExecutor),
      accountRepository_(accountRepository),
      accountPlayerRepository_(accountPlayerRepository),
      playerRepository_(playerRepository)
{
}

void CharacterListService::LoadCharacters(
    AccountId authenticatedAccountId,
    CompletionHandler completionHandler)
{
    if (!completionHandler)
    {
        throw std::invalid_argument(
            "Character list completion handler must not be empty");
    }

    if (authenticatedAccountId == 0)
    {
        PostCompletion(
            ioContext_,
            std::move(completionHandler),
            {CharacterListStatus::AccountNotFound, {}});
        return;
    }

    boost::asio::io_context* ioContext = &ioContext_;
    AccountRepository* accountRepository = &accountRepository_;
    AccountPlayerRepository* accountPlayerRepository =
        &accountPlayerRepository_;
    PlayerRepository* playerRepository = &playerRepository_;
    CompletionHandler rejectedCompletion = completionHandler;

    const bool queued = databaseExecutor_.TryPost(
        [ioContext,
         accountRepository,
         accountPlayerRepository,
         playerRepository,
         authenticatedAccountId,
         completionHandler = std::move(completionHandler)]() mutable
        {
            CharacterListResult result;

            try
            {
                if (!accountRepository->FindAccount(
                        authenticatedAccountId).has_value())
                {
                    result.status = CharacterListStatus::AccountNotFound;
                    PostCompletion(
                        *ioContext,
                        std::move(completionHandler),
                        std::move(result));
                    return;
                }

                const std::vector<PlayerId> playerIds =
                    accountPlayerRepository->FindPlayerIds(
                        authenticatedAccountId);
                result.characters.reserve(playerIds.size());

                for (const PlayerId playerId : playerIds)
                {
                    const std::optional<PlayerProfile> profile =
                        playerRepository->FindPlayer(playerId);
                    if (!profile.has_value())
                    {
                        throw std::runtime_error(
                            "Owned player profile was not found");
                    }

                    result.characters.push_back({
                        profile->playerId,
                        profile->name,
                        profile->level});
                }

                result.status = CharacterListStatus::Success;
            }
            catch (...)
            {
                result.status = CharacterListStatus::ServiceError;
                result.characters.clear();
            }

            PostCompletion(
                *ioContext,
                std::move(completionHandler),
                std::move(result));
        });

    if (!queued)
    {
        PostCompletion(
            ioContext_,
            std::move(rejectedCompletion),
            {CharacterListStatus::ServiceBusy, {}});
    }
}
} // namespace dnf
