#pragma once

#include "ChannelManager.h"
#include "DungeonCatalog.h"
#include "DungeonManager.h"
#include "DungeonUdpManager.h"
#include "EnemyCatalog.h"
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

    const EnemyCatalog& Enemies() const;
    const DungeonCatalog& DungeonTemplates() const;
    const DungeonManager& DungeonInstances() const;
    const DungeonUdpManager& DungeonUdpSockets() const;

private:
    void LoadGameData();

    boost::asio::io_context ioContext_;
    DungeonUdpManager dungeonUdpManager_;
    ChannelManager channelManager_;
    PartyManager partyManager_;
    EnemyCatalog enemyCatalog_;
    DungeonCatalog dungeonCatalog_;
    DungeonManager dungeonManager_;
    SessionManager sessionManager_;
    TcpServer tcpServer_;
};
} // namespace dnf
