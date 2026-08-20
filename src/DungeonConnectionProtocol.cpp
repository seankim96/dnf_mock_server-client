#include "DungeonConnectionProtocol.h"
#include "TcpFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <stdexcept>

namespace dnf
{
namespace
{
namespace tcp = Dnf::Protocol::Tcp;

bool IsValidResponse(
    DungeonConnectionInfoResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken)
{
    if (result < DungeonConnectionInfoResult::Success ||
        result > DungeonConnectionInfoResult::UdpNotReady)
    {
        return false;
    }

    const bool succeeded =
        result == DungeonConnectionInfoResult::Success;
    return succeeded == (dungeonId != 0) &&
           succeeded == (udpPort != 0) &&
           succeeded == (udpToken != 0);
}
} // namespace

void ValidateDungeonConnectionInfoRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_DungeonConnectionInfoRequest);
    if (message->payload_as_DungeonConnectionInfoRequest() == nullptr)
    {
        throw std::runtime_error(
            "Invalid dungeon connection info request payload");
    }
}

std::vector<std::uint8_t> EncodeDungeonConnectionInfoRequestPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request =
        tcp::CreateDungeonConnectionInfoRequest(builder);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_DungeonConnectionInfoRequest,
        request.Union());
}

std::vector<std::uint8_t> EncodeDungeonConnectionInfoResponsePayload(
    DungeonConnectionInfoResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken)
{
    if (!IsValidResponse(result, dungeonId, udpPort, udpToken))
    {
        throw std::invalid_argument(
            "Invalid dungeon connection info response");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto response = tcp::CreateDungeonConnectionInfoResponse(
        builder,
        static_cast<tcp::DungeonConnectionInfoResult>(result),
        dungeonId,
        udpPort,
        udpToken);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_DungeonConnectionInfoResponse,
        response.Union());
}

DungeonConnectionInfoData DecodeDungeonConnectionInfoResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_DungeonConnectionInfoResponse);
    const auto* response =
        message->payload_as_DungeonConnectionInfoResponse();
    if (response == nullptr)
    {
        throw std::runtime_error(
            "Invalid dungeon connection info response payload");
    }

    const auto result =
        static_cast<DungeonConnectionInfoResult>(response->result());
    const DungeonId dungeonId = response->dungeon_id();
    const std::uint16_t udpPort = response->udp_port();
    const DungeonUdpToken udpToken = response->udp_token();

    if (!IsValidResponse(result, dungeonId, udpPort, udpToken))
    {
        throw std::runtime_error(
            "Invalid dungeon connection info response data");
    }

    return {result, dungeonId, udpPort, udpToken};
}
} // namespace dnf
