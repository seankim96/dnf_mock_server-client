#include "PlayerLoginService.h"

#include <boost/asio/post.hpp>

#include <stdexcept>
#include <utility>

namespace dnf
{
namespace
{
void PostCompletion(
    boost::asio::io_context& ioContext,
    PlayerLoginService::CompletionHandler completionHandler,
    PlayerLoginResult result)
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

PlayerLoginService::PlayerLoginService(
    boost::asio::io_context& ioContext,
    DatabaseExecutor& databaseExecutor,
    AuthTicketVerifier& ticketVerifier,
    PlayerRepository& playerRepository)
    : ioContext_(ioContext),
      databaseExecutor_(databaseExecutor),
      ticketVerifier_(ticketVerifier),
      playerRepository_(playerRepository)
{
}

void PlayerLoginService::Login(
    std::string ticket,
    CompletionHandler completionHandler)
{
    if (!completionHandler)
    {
        throw std::invalid_argument(
            "Player login completion handler must not be empty");
    }

    boost::asio::io_context* ioContext = &ioContext_;
    AuthTicketVerifier* ticketVerifier = &ticketVerifier_;
    PlayerRepository* playerRepository = &playerRepository_;

    databaseExecutor_.Post(
        [ioContext,
         ticketVerifier,
         playerRepository,
         ticket = std::move(ticket),
         completionHandler = std::move(completionHandler)]() mutable
        {
            PlayerLoginResult result;

            try
            {
                result.authContext = ticketVerifier->Verify(ticket);
                if (!result.authContext.has_value())
                {
                    result.status = PlayerLoginStatus::InvalidTicket;
                    PostCompletion(
                        *ioContext,
                        std::move(completionHandler),
                        std::move(result));
                    return;
                }

                result.profile =
                    playerRepository->FindPlayer(
                        result.authContext->playerId);
                result.status = result.profile.has_value()
                    ? PlayerLoginStatus::Success
                    : PlayerLoginStatus::PlayerNotFound;
            }
            catch (...)
            {
                result.status = PlayerLoginStatus::StorageError;
                result.profile = std::nullopt;
            }

            PostCompletion(
                *ioContext,
                std::move(completionHandler),
                std::move(result));
        });
}
} // namespace dnf
