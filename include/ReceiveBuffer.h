#pragma once

#include "Packet.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace dnf
{
class ReceiveBuffer
{
public:
    // 새로 수신한 바이트를 기존 데이터 뒤에 붙인다.
    void Append(const std::vector<std::uint8_t>& data);

    // 완성된 패킷이 있으면 packet에 저장하고 true를 반환한다.
    // 아직 데이터가 부족하면 false를 반환한다.
    bool TryPop(Packet& packet);

    std::size_t Size() const;

private:
    std::vector<std::uint8_t> buffer_;
};
} // namespace dnf
