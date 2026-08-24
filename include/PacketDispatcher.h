#pragma once

#include "ChannelPartyRequestHandler.h"
#include "DungeonDataRequestHandler.h"
#include "Packet.h"
#include "SessionAuthState.h"
#include "SessionId.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace dnf
{
class ChannelManager;
class DungeonManager;
class DungeonUdpManager;
class PartyManager;
class PlayerLoginService;

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
        PlayerLoginService& playerLoginService,
        SessionId sessionId);

    // 테스트와 즉시 처리 가능한 요청에서 사용하는 동기 경로다.
    // 로그인처럼 DB 작업이 필요한 요청은 DispatchAsync를 사용한다.
    std::vector<std::uint8_t> Dispatch(const Packet& request) const;

    // 응답이 준비되면 responseHandler를 호출한다.
    // 현재 동기 핸들러도 이 경로를 사용하며, 이후 DB 기반 핸들러는
    // 작업 완료 시점에 같은 콜백을 호출할 수 있다.
    void DispatchAsync(
        Packet request,
        ResponseHandler responseHandler) const;

    std::optional<SessionAuthSnapshot> AuthSnapshot() const;

private:
    void HandleLoginRequestAsync(
        Packet request,
        ResponseHandler responseHandler) const;
    std::vector<std::uint8_t> HandleEnterDungeonRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleDungeonConnectionInfoRequest(
        const Packet& request) const;

    PartyManager& partyManager_;
    DungeonManager& dungeonManager_;
    DungeonUdpManager& dungeonUdpManager_;
    PlayerLoginService& playerLoginService_;
    std::shared_ptr<SessionAuthState> authState_;
    SessionId sessionId_;
    ChannelPartyRequestHandler channelPartyRequestHandler_;
    DungeonDataRequestHandler dungeonDataRequestHandler_;
};
} // namespace dnf
