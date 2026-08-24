#pragma once

#include "Packet.h"
#include "SessionId.h"

#include <cstdint>
#include <vector>

namespace dnf
{
class DungeonManager;

// 던전 카탈로그와 인스턴스의 정적 데이터만 조회한다.
class DungeonDataRequestHandler
{
public:
    DungeonDataRequestHandler(
        DungeonManager& dungeonManager,
        SessionId sessionId);

    std::vector<std::uint8_t> Dispatch(const Packet& request) const;

private:
    std::vector<std::uint8_t> HandleDungeonCatalogRequest(
        const Packet& request) const;
    std::vector<std::uint8_t> HandleDungeonStaticDataRequest(
        const Packet& request) const;

    DungeonManager& dungeonManager_;
    SessionId sessionId_;
};
} // namespace dnf
