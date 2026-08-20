#include "DungeonAdmissionProtocol.h"
#include "TcpFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <stdexcept>

namespace dnf
{
namespace
{
namespace tcp = Dnf::Protocol::Tcp;

bool IsValidResult(EnterDungeonResult result)
{
    return result >= EnterDungeonResult::Success &&
           result <= EnterDungeonResult::UdpAllocationFailed;
}

bool IsValidResponse(
    EnterDungeonResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken)
{
    if (!IsValidResult(result))
    {
        return false;
    }

    const bool succeeded = result == EnterDungeonResult::Success;
    return succeeded == (dungeonId != 0) &&
           succeeded == (udpPort != 0) &&
           succeeded == (udpToken != 0);
}
} // namespace

std::vector<std::uint8_t> EncodeEnterDungeonRequestPayload(
    DungeonTemplateId templateId)
{
    if (templateId == 0)
    {
        throw std::invalid_argument("Dungeon template ID must not be zero");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto request = tcp::CreateEnterDungeonRequest(
        builder,
        templateId);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_EnterDungeonRequest,
        request.Union());
}

DungeonTemplateId DecodeEnterDungeonRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_EnterDungeonRequest);
    const auto* request = message->payload_as_EnterDungeonRequest();
    if (request == nullptr || request->dungeon_template_id() == 0)
    {
        throw std::runtime_error("Invalid enter dungeon request data");
    }

    return request->dungeon_template_id();
}

std::vector<std::uint8_t> EncodeEnterDungeonResponsePayload(
    EnterDungeonResult result,
    DungeonId dungeonId,
    std::uint16_t udpPort,
    DungeonUdpToken udpToken)
{
    if (!IsValidResponse(result, dungeonId, udpPort, udpToken))
    {
        throw std::invalid_argument("Invalid enter dungeon response");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto response = tcp::CreateEnterDungeonResponse(
        builder,
        static_cast<tcp::EnterDungeonResult>(result),
        dungeonId,
        udpPort,
        udpToken);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_EnterDungeonResponse,
        response.Union());
}

EnterDungeonResponseData DecodeEnterDungeonResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_EnterDungeonResponse);
    const auto* response = message->payload_as_EnterDungeonResponse();
    if (response == nullptr)
    {
        throw std::runtime_error("Invalid enter dungeon response payload");
    }

    const auto result =
        static_cast<EnterDungeonResult>(response->result());
    const DungeonId dungeonId = response->dungeon_id();
    const std::uint16_t udpPort = response->udp_port();
    const DungeonUdpToken udpToken = response->udp_token();

    if (!IsValidResponse(result, dungeonId, udpPort, udpToken))
    {
        throw std::runtime_error("Invalid enter dungeon response data");
    }

    return {result, dungeonId, udpPort, udpToken};
}
} // namespace dnf
