#pragma once

#include "ChannelManager.h"
#include "Packet.h"
#include "SessionId.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace dnf
{
class DungeonManager;
class DungeonUdpManager;
class PartyManager;

class PacketDispatcher
{
public:
    using ResponseHandler =
        std::function<void(std::vector<std::uint8_t>)>;

    PacketDispatcher(
        ChannelManager& channelManager,
        PartyManager& partyManager,
        DungeonManager& dungeonManager,
        DungeonUdpManager& dungeonUdpManager,
        SessionId sessionId);

    // 요청 타입에 맞는 핸들러를 실행하고 응답 패킷을 반환한다.
    std::vector<std::uint8_t> Dispatch(const Packet& request) const;

    // 응답이 준비되면 responseHandler를 호출한다.
    // 현재 동기 핸들러도 이 경로를 사용하며, 이후 DB 기반 핸들러는
    // 작업 완료 시점에 같은 콜백을 호출할 수 있다.
    void DispatchAsync(
        Packet request,
        ResponseHandler responseHandler) const;

private:
    std::vector<std::uint8_t> HandleLoginRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleChannelListRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleJoinChannelRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleEnterDungeonRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleDungeonConnectionInfoRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleCreatePartyRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleJoinPartyRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleLeavePartyRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandlePartySnapshotRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleDungeonCatalogRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleDungeonStaticDataRequest(
        const Packet& request) const;

    ChannelManager& channelManager_;
    PartyManager& partyManager_;
    DungeonManager& dungeonManager_;
    DungeonUdpManager& dungeonUdpManager_;
    SessionId sessionId_;
};
} // namespace dnf
