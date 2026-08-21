#pragma once

#include "AccountId.h"
#include "PlayerId.h"

#include <vector>

namespace dnf
{
// 이 인터페이스의 함수는 DatabaseExecutor 작업 스레드에서 호출한다.
class AccountPlayerRepository
{
public:
    virtual ~AccountPlayerRepository() = default;

    virtual bool LinkPlayer(
        AccountId accountId,
        PlayerId playerId) = 0;
    virtual bool OwnsPlayer(
        AccountId accountId,
        PlayerId playerId) = 0;
    virtual std::vector<PlayerId> FindPlayerIds(
        AccountId accountId) = 0;
};
} // namespace dnf
