#include "ReceiveBuffer.h"

#include <algorithm>
#include <array>

namespace dnf
{
namespace
{
constexpr std::size_t COMPACT_THRESHOLD = 4096;
}

void ReceiveBuffer::Append(std::span<const std::uint8_t> data)
{
    CompactIfNeeded();
    buffer_.insert(buffer_.end(), data.begin(), data.end());
}

bool ReceiveBuffer::TryPop(Packet& packet)
{
    const std::size_t availableSize = Size();

    // 헤더 8바이트가 모두 도착할 때까지 기다린다.
    if (availableSize < PACKET_HEADER_SIZE)
    {
        return false;
    }

    std::array<std::uint8_t, PACKET_HEADER_SIZE> headerBytes{};
    const auto packetBegin = buffer_.begin() +
        static_cast<std::ptrdiff_t>(readOffset_);
    std::copy_n(packetBegin, PACKET_HEADER_SIZE, headerBytes.begin());

    const PacketHeader header = DecodeHeader(headerBytes);

    // 헤더는 도착했지만 Payload가 아직 덜 도착했다.
    if (availableSize < header.packetSize)
    {
        return false;
    }

    packet.header = header;
    packet.payload.assign(
        packetBegin + PACKET_HEADER_SIZE,
        packetBegin + header.packetSize);

    readOffset_ += header.packetSize;

    if (readOffset_ == buffer_.size())
    {
        buffer_.clear();
        readOffset_ = 0;
    }

    return true;
}

std::size_t ReceiveBuffer::Size() const
{
    return buffer_.size() - readOffset_;
}

void ReceiveBuffer::CompactIfNeeded()
{
    if (readOffset_ < COMPACT_THRESHOLD ||
        readOffset_ * 2 < buffer_.size())
    {
        return;
    }

    buffer_.erase(
        buffer_.begin(),
        buffer_.begin() + static_cast<std::ptrdiff_t>(readOffset_));
    readOffset_ = 0;
}
} // namespace dnf
