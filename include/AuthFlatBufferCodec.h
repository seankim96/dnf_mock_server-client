#pragma once

#include "AuthMessage_generated.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <cstdint>
#include <vector>

namespace dnf
{
constexpr std::uint16_t AUTH_PAYLOAD_PROTOCOL_VERSION = 1;

std::vector<std::uint8_t> FinishAuthPayload(
    flatbuffers::FlatBufferBuilder& builder,
    Dnf::Protocol::Auth::AuthPayload payloadType,
    flatbuffers::Offset<void> payload);

const Dnf::Protocol::Auth::AuthMessage* DecodeAuthPayload(
    const std::vector<std::uint8_t>& bytes,
    Dnf::Protocol::Auth::AuthPayload expectedType);
} // namespace dnf
