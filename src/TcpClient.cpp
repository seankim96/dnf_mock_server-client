#include "TcpClient.h"

#include <boost/asio/buffer.hpp>
#include <boost/asio/ip/address.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace dnf
{
using boost::asio::ip::tcp;

TcpClient::TcpClient()
    : socket_(ioContext_)
{
}

TcpClient::~TcpClient()
{
    Close();
}

void TcpClient::Connect(const std::string& ip, std::uint16_t port)
{
    const auto address = boost::asio::ip::make_address(ip);
    socket_.connect(tcp::endpoint(address, port));
}

void TcpClient::Send(const std::vector<std::uint8_t>& data)
{
    boost::asio::write(socket_, boost::asio::buffer(data));
}

bool TcpClient::ReceivePacket(Packet& packet)
{
    std::array<std::uint8_t, 4096> receivedBytes{};

    while (true)
    {
        if (receiveBuffer_.TryPop(packet))
        {
            return true;
        }

        boost::system::error_code error;
        const std::size_t receivedSize = socket_.read_some(
            boost::asio::buffer(receivedBytes), error);

        if (error == boost::asio::error::eof)
        {
            if (receiveBuffer_.Size() != 0)
            {
                throw std::runtime_error(
                    "Server disconnected during packet receive");
            }

            return false;
        }

        if (error)
        {
            throw boost::system::system_error(error);
        }

        const std::vector<std::uint8_t> data(
            receivedBytes.begin(),
            receivedBytes.begin() + receivedSize);
        receiveBuffer_.Append(data);
    }
}

void TcpClient::Close()
{
    if (!socket_.is_open())
    {
        return;
    }

    boost::system::error_code ignoredError;
    socket_.shutdown(tcp::socket::shutdown_both, ignoredError);
    socket_.close(ignoredError);
}
} // namespace dnf
