#pragma once

#include "Packet.h"
#include "ReceiveBuffer.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace dnf
{
class TcpClient
{
public:
    TcpClient();
    ~TcpClient();

    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    void Connect(const std::string& ip, std::uint16_t port);
    void Send(const std::vector<std::uint8_t>& data);
    bool ReceivePacket(Packet& packet);
    void Close();

private:
    boost::asio::io_context ioContext_;
    boost::asio::ip::tcp::socket socket_;
    ReceiveBuffer receiveBuffer_;
};
} // namespace dnf
