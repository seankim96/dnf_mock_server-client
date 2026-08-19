#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace dnf
{
constexpr std::uint16_t PACKET_HEADER_SIZE = 8;
constexpr std::uint16_t MAX_PACKET_SIZE = 16 * 1024;

// 클라이언트가 어떤 요청을 보냈는지 구분한다.
enum PacketType : std::uint16_t
{
    LoginRequest = 1,
    ChannelListRequest = 2,
    JoinChannelRequest = 3,
    EnterDungeonRequest = 4,
    DungeonConnectionInfoRequest = 5,
    CreatePartyRequest = 6,

    LoginResponse = 101,
    ChannelListResponse = 102,
    JoinChannelResponse = 103,
    EnterDungeonResponse = 104,
    DungeonConnectionInfoResponse = 105,
    CreatePartyResponse = 106
};

// 모든 TCP 패킷 앞에 붙는 공통 정보다.
struct PacketHeader
{
    // 헤더와 데이터를 합친 전체 패킷 크기
    std::uint16_t packetSize = 0;

    // 로그인, 채널 목록 등의 패킷 종류
    PacketType type = LoginRequest;

    // 요청과 응답을 연결하기 위한 번호
    std::uint32_t requestId = 0;
};

struct Packet
{
    PacketHeader header;
    std::vector<std::uint8_t> payload;
};

// PacketHeader를 네트워크로 보낼 수 있는 8바이트로 변환한다.
std::array<std::uint8_t, PACKET_HEADER_SIZE> EncodeHeader(
    const PacketHeader& header);

// 수신한 8바이트를 PacketHeader로 복원한다.
PacketHeader DecodeHeader(
    const std::array<std::uint8_t, PACKET_HEADER_SIZE>& bytes);

// 헤더와 Payload를 하나의 전송 데이터로 만든다.
std::vector<std::uint8_t> EncodePacket(
    PacketType type,
    std::uint32_t requestId,
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
