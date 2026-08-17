#include "ReceiveBuffer.h"

#include <algorithm>
#include <array>

namespace dnf
{
void ReceiveBuffer::Append(const std::vector<std::uint8_t>& data)
{
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

bool ReceiveBuffer::TryPop(Packet& packet)
{
    // 헤더 8바이트가 모두 도착할 때까지 기다린다.
    if (buffer_.size() < PACKET_HEADER_SIZE)
    {
        return false;
    }

    std::array<std::uint8_t, PACKET_HEADER_SIZE> headerBytes{};
    std::copy_n(buffer_.begin(), PACKET_HEADER_SIZE, headerBytes.begin());

    const PacketHeader header = DecodeHeader(headerBytes);

    // 헤더는 도착했지만 Payload가 아직 덜 도착했다.
    if (buffer_.size() < header.packetSize)
    {
        return false;
    }

    packet.header = header;
    packet.payload.assign(
        buffer_.begin() + PACKET_HEADER_SIZE,
        buffer_.begin() + header.packetSize);

    // 꺼낸 패킷은 수신 버퍼에서 제거한다.
    buffer_.erase(buffer_.begin(), buffer_.begin() + header.packetSize);
    return true;
}

std::size_t ReceiveBuffer::Size() const
{
    return buffer_.size();
}
} // namespace dnf
