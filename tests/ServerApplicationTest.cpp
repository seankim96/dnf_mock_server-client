#include "ServerApplication.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/system/error_code.hpp>

#include <array>
#include <cassert>
#include <chrono>
#include <csignal>
#include <exception>
#include <filesystem>
#include <iostream>
#include <thread>

namespace
{
std::filesystem::path FindProjectDataDirectory()
{
    std::filesystem::path candidate = std::filesystem::current_path();
    for (int level = 0; level < 6; ++level)
    {
        if (std::filesystem::is_regular_file(
                candidate / "data" / "channels.json"))
        {
            return candidate / "data";
        }

        if (!candidate.has_parent_path())
        {
            break;
        }
        candidate = candidate.parent_path();
    }

    return "data";
}

void TestGameDataIsLoaded()
{
    dnf::ServerApplication application(
        0,
        ":memory:",
        FindProjectDataDirectory().string());

    const auto iceSlash = application.Skills().GetSkill(1001);
    assert(iceSlash.has_value());
    assert(iceSlash->name == "Ice Slash");
    assert(iceSlash->effects.size() == 2);

    const auto goblin = application.Enemies().GetEnemy(2001);
    assert(goblin.has_value());
    assert(goblin->name == "Goblin");
    assert(goblin->maxHp == 100);

    const auto forest = application.DungeonTemplates().GetDungeon(1001);
    assert(forest.has_value());
    assert(forest->name == "Forest");
    assert(forest->rooms.size() == 2);
    assert(forest->rooms[0].enemySpawns.size() == 1);
    assert(forest->rooms[0].enemySpawns[0].enemyTemplateId == 2001);

    assert(application.DungeonInstances().ActiveDungeonCount() == 0);
    assert(application.DungeonUdpSockets().AllocationCount() == 0);
}

void TestStopBeforeRunAndRepeatedStop()
{
    dnf::ServerApplication application(
        0,
        ":memory:",
        FindProjectDataDirectory().string());

    application.Stop();
    application.Stop();

    const auto startedAt = std::chrono::steady_clock::now();
    application.Run();
    const auto elapsed =
        std::chrono::steady_clock::now() - startedAt;

    assert(elapsed < std::chrono::seconds(1));
}

void TestRunningApplicationStopsPromptlyAndClosesTcpClient()
{
    using boost::asio::ip::tcp;

    dnf::ServerApplication application(
        0,
        ":memory:",
        FindProjectDataDirectory().string());
    std::exception_ptr serverException;

    std::thread serverThread(
        [&]
        {
            try
            {
                application.Run();
            }
            catch (...)
            {
                serverException = std::current_exception();
            }

        });

    const auto startupDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (application.Port() == 0 &&
           std::chrono::steady_clock::now() < startupDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (application.Port() == 0)
    {
        application.Stop();
        serverThread.join();
        assert(false && "Server did not bind within the deadline");
    }

    boost::asio::io_context clientIoContext;
    tcp::socket client(clientIoContext);
    client.connect(tcp::endpoint(
        boost::asio::ip::address_v4::loopback(),
        application.Port()));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    const auto shutdownStartedAt = std::chrono::steady_clock::now();
    application.Stop();
    application.Stop();

    serverThread.join();
    assert(
        std::chrono::steady_clock::now() - shutdownStartedAt <
        std::chrono::seconds(2));

    if (serverException)
    {
        std::rethrow_exception(serverException);
    }

    client.non_blocking(true);
    std::array<char, 1> byte{};
    boost::system::error_code readError;
    client.read_some(boost::asio::buffer(byte), readError);
    assert(readError != boost::asio::error::would_block);
    assert(readError != boost::asio::error::try_again);
}

void TestRunningApplicationStopsOnSignal()
{
    dnf::ServerApplication application(
        0,
        ":memory:",
        FindProjectDataDirectory().string());
    std::exception_ptr serverException;

    std::thread serverThread(
        [&]
        {
            try
            {
                application.Run();
            }
            catch (...)
            {
                serverException = std::current_exception();
            }
        });

    const auto startupDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (application.Port() == 0 &&
           std::chrono::steady_clock::now() < startupDeadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (application.Port() == 0)
    {
        application.Stop();
        serverThread.join();
        assert(false && "Server did not bind within the deadline");
    }

    assert(std::raise(SIGTERM) == 0);
    serverThread.join();

    if (serverException)
    {
        std::rethrow_exception(serverException);
    }
}
} // namespace

int main()
{
    TestGameDataIsLoaded();
    TestStopBeforeRunAndRepeatedStop();
    TestRunningApplicationStopsPromptlyAndClosesTcpClient();
    TestRunningApplicationStopsOnSignal();

    std::cout << "All server application tests passed.\n";
    return 0;
}
