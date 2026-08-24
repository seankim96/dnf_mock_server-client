#pragma once

#include "AccountId.h"
#include "AuthTicketIssuer.h"
#include "DatabaseExecutor.h"
#include "PlayerId.h"

#include <boost/asio/io_context.hpp>

#include <functional>
#include <optional>

namespace dnf
{
enum class CharacterSelectionStatus
{
    Success,
    InvalidSelection,
    ServiceBusy,
    ServiceError
};

struct CharacterSelectionResult
{
    CharacterSelectionStatus status =
        CharacterSelectionStatus::InvalidSelection;
    std::optional<IssuedAuthTicket> authTicket;
};

class CharacterSelectionService
{
public:
    using CompletionHandler =
        std::function<void(CharacterSelectionResult)>;

    CharacterSelectionService(
        boost::asio::io_context& ioContext,
        DatabaseExecutor& databaseExecutor,
        AuthTicketIssuer& ticketIssuer);

    void SelectCharacter(
        AccountId authenticatedAccountId,
        PlayerId selectedPlayerId,
        CompletionHandler completionHandler);

private:
    boost::asio::io_context& ioContext_;
    DatabaseExecutor& databaseExecutor_;
    AuthTicketIssuer& ticketIssuer_;
};
} // namespace dnf
