#pragma once

#include "ChannelManager.h"
#include "Packet.h"
#include "SessionId.h"

#include <cstdint>
#include <vector>

namespace dnf
{
class DungeonManager;
class PartyManager;

class PacketDispatcher
{
public:
    PacketDispatcher(
        ChannelManager& channelManager,
        PartyManager& partyManager,
        DungeonManager& dungeonManager,
        SessionId sessionId);

    // 요청 타입에 맞는 핸들러를 실행하고 응답 패킷을 반환한다.
    std::vector<std::uint8_t> Dispatch(const Packet& request) const;

private:
    std::vector<std::uint8_t> HandleLoginRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleChannelListRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleJoinChannelRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleEnterDungeonRequest(
        const Packet& request) const;

    ChannelManager& channelManager_;
    PartyManager& partyManager_;
    DungeonManager& dungeonManager_;
    SessionId sessionId_;
};
} // namespace dnf
