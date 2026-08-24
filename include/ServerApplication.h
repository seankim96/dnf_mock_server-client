#pragma once

#include "ChannelManager.h"
#include "DatabaseExecutor.h"
#include "DungeonCatalog.h"
#include "DungeonManager.h"
#include "DungeonTickService.h"
#include "DungeonUdpManager.h"
#include "EnemyCatalog.h"
#include "GameDataLoader.h"
#include "PartyManager.h"
#include "PlayerLoginService.h"
#include "SessionManager.h"
#include "SkillCatalog.h"
#include "SqliteAuthTicketStore.h"
#include "SqliteAuthTicketVerifier.h"
#include "SqliteDatabase.h"
#include "SqlitePlayerRepository.h"
#include "TcpServer.h"

#include <boost/asio/io_context.hpp>

#include <cstdint>
#include <string>

namespace dnf
{
class ServerApplication
{
public:
    ServerApplication(
        std::uint16_t port,
        const std::string& databasePath = "dnf_mock_server.db",
        const std::string& dataDirectory = "data");

    void Run();

    const SkillCatalog& Skills() const;
    const EnemyCatalog& Enemies() const;
    const DungeonCatalog& DungeonTemplates() const;
    const DungeonManager& DungeonInstances() const;
    const DungeonUdpManager& DungeonUdpSockets() const;

private:
    void LoadGameData(const std::string& dataDirectory);

    boost::asio::io_context ioContext_;
    SqliteDatabase database_;
    SqlitePlayerRepository playerRepository_;
    SqliteAuthTicketStore authTicketStore_;
    SqliteAuthTicketVerifier authTicketVerifier_;
    DatabaseExecutor databaseExecutor_;
    PlayerLoginService playerLoginService_;
    DungeonUdpManager dungeonUdpManager_;
    ChannelManager channelManager_;
    PartyManager partyManager_;
    SkillCatalog skillCatalog_;
    EnemyCatalog enemyCatalog_;
    DungeonCatalog dungeonCatalog_;
    GameDataLoadResult gameData_;
    DungeonManager dungeonManager_;
    DungeonTickService dungeonTickService_;
    SessionManager sessionManager_;
    TcpServer tcpServer_;
};
} // namespace dnf
