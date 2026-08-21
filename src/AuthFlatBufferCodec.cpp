#include "AuthFlatBufferCodec.h"

#include <flatbuffers/verifier.h>

#include <stdexcept>

namespace dnf
{
std::vector<std::uint8_t> FinishAuthPayload(
    flatbuffers::FlatBufferBuilder& builder,
    Dnf::Protocol::Auth::AuthPayload payloadType,
    flatbuffers::Offset<void> payload)
{
    if (payloadType == Dnf::Protocol::Auth::AuthPayload_NONE ||
        payload.IsNull())
    {
        throw std::invalid_argument("Auth payload must not be empty");
    }

    const auto message = Dnf::Protocol::Auth::CreateAuthMessage(
        builder,
        AUTH_PAYLOAD_PROTOCOL_VERSION,
        payloadType,
        payload);
    Dnf::Protocol::Auth::FinishAuthMessageBuffer(builder, message);

    return std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
}

const Dnf::Protocol::Auth::AuthMessage* DecodeAuthPayload(
    const std::vector<std::uint8_t>& bytes,
    Dnf::Protocol::Auth::AuthPayload expectedType)
{
    if (bytes.empty())
    {
        throw std::runtime_error("Auth FlatBuffer payload is empty");
    }

    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!Dnf::Protocol::Auth::VerifyAuthMessageBuffer(verifier))
    {
        throw std::runtime_error("Invalid Auth FlatBuffer payload");
    }

    const auto* message =
        Dnf::Protocol::Auth::GetAuthMessage(bytes.data());
    if (message->protocol_version() != AUTH_PAYLOAD_PROTOCOL_VERSION)
    {
        throw std::runtime_error(
            "Unsupported Auth payload protocol version");
    }

    if (message->payload_type() != expectedType ||
        message->payload() == nullptr)
    {
        throw std::runtime_error(
            "Unexpected Auth FlatBuffer payload type");
    }

    return message;
}
} // namespace dnf
