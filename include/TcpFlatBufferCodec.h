#pragma once

#include "TcpMessage_generated.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <cstdint>
#include <vector>

namespace dnf
{
constexpr std::uint16_t TCP_PAYLOAD_PROTOCOL_VERSION = 1;

std::vector<std::uint8_t> FinishTcpPayload(
    flatbuffers::FlatBufferBuilder& builder,
    Dnf::Protocol::Tcp::TcpPayload payloadType,
    flatbuffers::Offset<void> payload);

const Dnf::Protocol::Tcp::TcpMessage* DecodeTcpPayload(
    const std::vector<std::uint8_t>& bytes,
    Dnf::Protocol::Tcp::TcpPayload expectedType);
} // namespace dnf
