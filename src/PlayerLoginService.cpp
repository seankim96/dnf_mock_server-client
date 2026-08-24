#include "PlayerLoginService.h"

#include <boost/asio/post.hpp>

#include <openssl/crypto.h>

#include <memory>
#include <stdexcept>
#include <utility>

namespace dnf
{
namespace
{
struct PlayerLoginWork
{
    ~PlayerLoginWork()
    {
        OPENSSL_cleanse(ticket.data(), ticket.size());
    }

    std::string ticket;
    PlayerLoginService::CompletionHandler completionHandler;
};

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
    auto work = std::make_shared<PlayerLoginWork>();
    work->ticket = std::move(ticket);
    work->completionHandler = std::move(completionHandler);

    const bool queued = databaseExecutor_.TryPost(
        [ioContext,
         ticketVerifier,
         playerRepository,
         work]() mutable
        {
            PlayerLoginResult result;

            try
            {
                result.authContext =
                    ticketVerifier->Verify(work->ticket);
                if (!result.authContext.has_value())
                {
                    result.status = PlayerLoginStatus::InvalidTicket;
                    PostCompletion(
                        *ioContext,
                        std::move(work->completionHandler),
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
                std::move(work->completionHandler),
                std::move(result));
        });

    if (!queued)
    {
        PostCompletion(
            ioContext_,
            std::move(work->completionHandler),
            {PlayerLoginStatus::ServiceBusy,
             std::nullopt,
             std::nullopt});
    }
}
} // namespace dnf
