#include "Packet.h"
#include "TcpClient.h"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
