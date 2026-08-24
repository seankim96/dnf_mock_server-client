#pragma once

#include "AuthTicketVerifier.h"
#include "DatabaseExecutor.h"
#include "PlayerProfile.h"
#include "PlayerRepository.h"

#include <boost/asio/io_context.hpp>

#include <functional>
#include <optional>
#include <string>

namespace dnf
{
enum class PlayerLoginStatus
{
    Success,
    InvalidTicket,
    PlayerNotFound,
    ServiceBusy,
    StorageError
};

struct PlayerLoginResult
{
    PlayerLoginStatus status = PlayerLoginStatus::InvalidTicket;
    std::optional<AuthContext> authContext;
    std::optional<PlayerProfile> profile;
};

class PlayerLoginService
{
public:
    using CompletionHandler = std::function<void(PlayerLoginResult)>;

    PlayerLoginService(
        boost::asio::io_context& ioContext,
        DatabaseExecutor& databaseExecutor,
        AuthTicketVerifier& ticketVerifier,
        PlayerRepository& playerRepository);

    void Login(
        std::string ticket,
        CompletionHandler completionHandler);

private:
    boost::asio::io_context& ioContext_;
    DatabaseExecutor& databaseExecutor_;
    AuthTicketVerifier& ticketVerifier_;
    PlayerRepository& playerRepository_;
};
} // namespace dnf
