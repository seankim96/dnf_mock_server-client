#pragma once

#include "ChannelManager.h"
#include "PartyManager.h"
#include "SessionManager.h"
#include "TcpServer.h"

#include <boost/asio/io_context.hpp>

#include <cstdint>

namespace dnf
{
class ServerApplication
{
public:
    explicit ServerApplication(std::uint16_t port);

    void Run();

private:
    boost::asio::io_context ioContext_;
    ChannelManager channelManager_;
    PartyManager partyManager_;
    SessionManager sessionManager_;
    TcpServer tcpServer_;
};
} // namespace dnf
