#pragma once

#include "Packet.h"
#include "SessionId.h"

#include <cstdint>
#include <vector>

namespace dnf
{
class ChannelManager;
class PartyManager;

// 로비에서 채널과 파티 요청만 처리한다.
class ChannelPartyRequestHandler
{
public:
    ChannelPartyRequestHandler(
        ChannelManager& channelManager,
        PartyManager& partyManager,
        SessionId sessionId);

    std::vector<std::uint8_t> Dispatch(const Packet& request) const;

private:
    std::vector<std::uint8_t> HandleChannelListRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleJoinChannelRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleCreatePartyRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleJoinPartyRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleLeavePartyRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandlePartySnapshotRequest(
        const Packet& request) const;

    ChannelManager& channelManager_;
    PartyManager& partyManager_;
    SessionId sessionId_;
};
} // namespace dnf
