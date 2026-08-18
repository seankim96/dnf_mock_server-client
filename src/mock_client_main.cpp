#include "ChannelProtocol.h"
#include "Packet.h"
#include "TcpClient.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
const char* JoinResultToText(dnf::JoinChannelResult result)
{
    switch (result)
    {
    case dnf::JoinChannelResult::Success:
        return "Success";
    case dnf::JoinChannelResult::ChannelNotFound:
        return "ChannelNotFound";
    case dnf::JoinChannelResult::ChannelFull:
        return "ChannelFull";
    case dnf::JoinChannelResult::AlreadyJoined:
        return "AlreadyJoined";
    }

    return "Unknown";
}
} // namespace

int main(int argc, char* argv[])
{
    try
    {
        unsigned long portValue = 7777;

        if (argc >= 3)
        {
            portValue = std::stoul(argv[2]);
        }

        if (portValue == 0 || portValue > 65535)
        {
            throw std::runtime_error("Port must be between 1 and 65535");
        }

        const auto port = static_cast<std::uint16_t>(portValue);
        const std::string playerName =
            argc >= 2 ? argv[1] : "MockPlayer";

        unsigned long channelIdValue = 1;
        if (argc >= 4)
        {
            channelIdValue = std::stoul(argv[3]);
        }

        if (channelIdValue > UINT32_MAX)
        {
            throw std::runtime_error("Channel ID is too large");
        }

        const auto channelId = static_cast<dnf::ChannelId>(channelIdValue);

        const std::vector<std::uint8_t> payload(
            playerName.begin(), playerName.end());
        const auto request = dnf::EncodePacket(
            dnf::LoginRequest, 1, payload);

        dnf::TcpClient client;
        client.Connect("127.0.0.1", port);
        client.Send(request);

        std::cout << "Connected to 127.0.0.1:" << port << '\n';
        std::cout << "LoginRequest sent\n";
        std::cout << "Player name: " << playerName << '\n';

        dnf::Packet response;
        if (!client.ReceivePacket(response))
        {
            std::cerr << "Server closed before sending LoginResponse\n";
            return 1;
        }

        if (response.header.type != dnf::LoginResponse ||
            response.header.requestId != 1 ||
            response.payload.size() != 1)
        {
            throw std::runtime_error("Invalid LoginResponse");
        }

        std::cout << "LoginResponse received\n";
        std::cout << "Request ID: " << response.header.requestId << '\n';
        std::cout << "Result: " << static_cast<int>(response.payload[0]) << '\n';

        if (response.payload[0] != 0)
        {
            return 1;
        }

        const auto channelListRequest = dnf::EncodePacket(
            dnf::ChannelListRequest, 2, {});
        client.Send(channelListRequest);
        std::cout << "ChannelListRequest sent\n";

        if (!client.ReceivePacket(response))
        {
            std::cerr << "Server closed before sending ChannelListResponse\n";
            return 1;
        }

        if (response.header.type != dnf::ChannelListResponse ||
            response.header.requestId != 2)
        {
            throw std::runtime_error("Invalid ChannelListResponse");
        }

        const auto channels = dnf::DecodeChannelListPayload(response.payload);

        std::cout << "ChannelListResponse received\n";
        for (const dnf::ChannelListEntry& channel : channels)
        {
            std::cout << "Channel " << channel.id
                      << " players=" << channel.currentPlayers
                      << '/' << channel.maxPlayers << '\n';
        }

        const auto joinPayload =
            dnf::EncodeJoinChannelRequestPayload(channelId);
        const auto joinRequest = dnf::EncodePacket(
            dnf::JoinChannelRequest, 3, joinPayload);
        client.Send(joinRequest);

        std::cout << "JoinChannelRequest sent"
                  << " channelId=" << channelId << '\n';

        if (!client.ReceivePacket(response))
        {
            std::cerr << "Server closed before sending JoinChannelResponse\n";
            return 1;
        }

        if (response.header.type != dnf::JoinChannelResponse ||
            response.header.requestId != 3)
        {
            throw std::runtime_error("Invalid JoinChannelResponse");
        }

        const auto joinResponse =
            dnf::DecodeJoinChannelResponsePayload(response.payload);

        std::cout << "JoinChannelResponse received"
                  << " channelId=" << joinResponse.channelId
                  << " result=" << JoinResultToText(joinResponse.result)
                  << '\n';

        if (joinResponse.result != dnf::JoinChannelResult::Success)
        {
            return 1;
        }

        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
