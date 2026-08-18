#include "DungeonTickService.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

#include <cassert>
#include <chrono>
#include <iostream>

namespace
{
void TestTickTimerRunsAndStops()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager udpManager(ioContext);
    dnf::PartyManager partyManager;
    dnf::EnemyCatalog enemyCatalog;
    dnf::DungeonCatalog dungeonCatalog(enemyCatalog);
    dnf::DungeonManager dungeonManager(
        partyManager,
        dungeonCatalog,
        enemyCatalog);

    dnf::DungeonTickService tickService(
        ioContext,
        dungeonManager,
        udpManager);

    tickService.Start();
    tickService.Start();
    assert(tickService.IsRunning());

    boost::asio::steady_timer stopTimer(
        ioContext,
        std::chrono::milliseconds(120));
    stopTimer.async_wait(
        [&tickService](const boost::system::error_code&)
        {
            tickService.Stop();
        });

    ioContext.run();

    assert(!tickService.IsRunning());
    assert(tickService.TickCount() >= 2);
}
} // namespace

int main()
{
    TestTickTimerRunsAndStops();

    std::cout << "All dungeon tick service tests passed.\n";
    return 0;
}
