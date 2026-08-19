#include "TcpFlatBufferCodec.h"

#include <flatbuffers/verifier.h>

#include <stdexcept>

namespace dnf
{
std::vector<std::uint8_t> FinishTcpPayload(
    flatbuffers::FlatBufferBuilder& builder,
    Dnf::Protocol::Tcp::TcpPayload payloadType,
    flatbuffers::Offset<void> payload)
{
    if (payloadType == Dnf::Protocol::Tcp::TcpPayload_NONE ||
        payload.IsNull())
    {
        throw std::invalid_argument("TCP payload must not be empty");
    }

    const auto message = Dnf::Protocol::Tcp::CreateTcpMessage(
        builder,
        TCP_PAYLOAD_PROTOCOL_VERSION,
        payloadType,
        payload);
    Dnf::Protocol::Tcp::FinishTcpMessageBuffer(builder, message);

    return std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
}

const Dnf::Protocol::Tcp::TcpMessage* DecodeTcpPayload(
    const std::vector<std::uint8_t>& bytes,
    Dnf::Protocol::Tcp::TcpPayload expectedType)
{
    if (bytes.empty())
    {
        throw std::runtime_error("TCP FlatBuffer payload is empty");
    }

    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!Dnf::Protocol::Tcp::VerifyTcpMessageBuffer(verifier))
    {
        throw std::runtime_error("Invalid TCP FlatBuffer payload");
    }

    const auto* message =
        Dnf::Protocol::Tcp::GetTcpMessage(bytes.data());
    if (message->protocol_version() != TCP_PAYLOAD_PROTOCOL_VERSION)
    {
        throw std::runtime_error("Unsupported TCP payload protocol version");
    }

    if (message->payload_type() != expectedType ||
        message->payload() == nullptr)
    {
        throw std::runtime_error("Unexpected TCP FlatBuffer payload type");
    }

    return message;
}
} // namespace dnf
