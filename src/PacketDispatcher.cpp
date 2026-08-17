#include "PacketDispatcher.h"

#include "LoginValidator.h"

#include <stdexcept>

namespace dnf
{
std::vector<std::uint8_t> PacketDispatcher::Dispatch(
    const Packet& request) const
{
    switch (request.header.type)
    {
    case LoginRequest:
        return HandleLoginRequest(request);

    default:
        throw std::runtime_error("No handler for packet type");
    }
}

std::vector<std::uint8_t> PacketDispatcher::HandleLoginRequest(
    const Packet& request) const
{
    const LoginValidator validator;
    const LoginValidationResult validation = validator.Validate(request.payload);

    const std::vector<std::uint8_t> responsePayload = {
        static_cast<std::uint8_t>(validation.result)};

    return EncodePacket(
        LoginResponse,
        request.header.requestId,
        responsePayload);
}
} // namespace dnf
