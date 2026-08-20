#pragma once

#include "PlayerProfile.h"

#include <optional>
#include <string>

namespace dnf
{
// 이 인터페이스의 함수는 DatabaseExecutor 작업 스레드에서 호출한다.
class PlayerRepository
{
public:
    virtual ~PlayerRepository() = default;

    virtual std::optional<PlayerProfile> FindPlayer(
        PlayerId playerId) = 0;
    virtual std::optional<PlayerProfile> FindPlayerByName(
        const std::string& playerName) = 0;
    virtual std::optional<PlayerProfile> CreatePlayer(
        const std::string& playerName) = 0;
    virtual bool SavePlayer(const PlayerProfile& profile) = 0;
};
} // namespace dnf
