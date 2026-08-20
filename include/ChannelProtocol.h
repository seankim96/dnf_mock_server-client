#pragma once

#include "ChannelManager.h"

#include <cstdint>
#include <vector>

namespace dnf
{
struct ChannelListEntry
{
    ChannelId id = 0;
    std::string name;
    std::uint32_t currentPlayers = 0;
    std::uint32_t maxPlayers = 0;
};

struct JoinChannelResponseData
{
    JoinChannelResult result = JoinChannelResult::Success;
    ChannelId channelId = 0;
};

void ValidateChannelListRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeChannelListRequestPayload();

std::vector<std::uint8_t> EncodeChannelListResponsePayload(
    const std::vector<ChannelInfo>& channels);

std::vector<ChannelListEntry> DecodeChannelListResponsePayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeJoinChannelRequestPayload(
    ChannelId channelId);

ChannelId DecodeJoinChannelRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeJoinChannelResponsePayload(
    JoinChannelResult result,
    ChannelId channelId);

JoinChannelResponseData DecodeJoinChannelResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
