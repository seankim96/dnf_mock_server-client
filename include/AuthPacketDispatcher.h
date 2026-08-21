#pragma once

#include "AccountId.h"
#include "AuthServerSessionState.h"
#include "Packet.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace dnf
{
class AccountAuthenticationService;
class CharacterListService;

class AuthPacketDispatcher
{
public:
    using ResponseHandler =
        std::function<void(std::vector<std::uint8_t>)>;

    explicit AuthPacketDispatcher(
        AccountAuthenticationService& authenticationService,
        CharacterListService& characterListService);

    void DispatchAsync(
        Packet request,
        ResponseHandler responseHandler) const;

    std::optional<AccountId> AuthenticatedAccount() const;

private:
    void HandleLoginRequestAsync(
        Packet request,
        ResponseHandler responseHandler) const;
    void HandleCharacterListRequestAsync(
        Packet request,
        ResponseHandler responseHandler) const;

    AccountAuthenticationService& authenticationService_;
    CharacterListService& characterListService_;
    std::shared_ptr<AuthServerSessionState> sessionState_;
    std::shared_ptr<std::atomic_bool> loginInProgress_;
};
} // namespace dnf
