#include "CharacterSelectionService.h"

#include <boost/asio/post.hpp>

#include <stdexcept>
#include <utility>

namespace dnf
{
namespace
{
void PostCompletion(
    boost::asio::io_context& ioContext,
    CharacterSelectionService::CompletionHandler completionHandler,
    CharacterSelectionResult result)
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

CharacterSelectionService::CharacterSelectionService(
    boost::asio::io_context& ioContext,
    DatabaseExecutor& databaseExecutor,
    AuthTicketIssuer& ticketIssuer)
    : ioContext_(ioContext),
      databaseExecutor_(databaseExecutor),
      ticketIssuer_(ticketIssuer)
{
}

void CharacterSelectionService::SelectCharacter(
    AccountId authenticatedAccountId,
    PlayerId selectedPlayerId,
    CompletionHandler completionHandler)
{
    if (!completionHandler)
    {
        throw std::invalid_argument(
            "Character selection completion handler must not be empty");
    }

    if (authenticatedAccountId == 0 || selectedPlayerId == 0)
    {
        PostCompletion(
            ioContext_,
            std::move(completionHandler),
            {CharacterSelectionStatus::InvalidSelection, std::nullopt});
        return;
    }

    boost::asio::io_context* ioContext = &ioContext_;
    AuthTicketIssuer* ticketIssuer = &ticketIssuer_;
    CompletionHandler rejectedCompletion = completionHandler;

    const bool queued = databaseExecutor_.TryPost(
        [ioContext,
         ticketIssuer,
         authenticatedAccountId,
         selectedPlayerId,
         completionHandler = std::move(completionHandler)]() mutable
        {
            CharacterSelectionResult result;

            try
            {
                result.authTicket = ticketIssuer->Issue(
                    {authenticatedAccountId, selectedPlayerId});
                result.status = result.authTicket.has_value()
                    ? CharacterSelectionStatus::Success
                    : CharacterSelectionStatus::InvalidSelection;
            }
            catch (...)
            {
                result.status = CharacterSelectionStatus::ServiceError;
                result.authTicket = std::nullopt;
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
            {CharacterSelectionStatus::ServiceBusy, std::nullopt});
    }
}
} // namespace dnf
