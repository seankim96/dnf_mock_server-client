#include "ServerApplication.h"

#include <exception>
#include <iostream>
#include <thread>
#include <vector>

namespace dnf
{
namespace
{
constexpr std::size_t IO_THREAD_COUNT = 4;
}

ServerApplication::ServerApplication(std::uint16_t port)
    : sessionManager_(channelManager_, partyManager_),
      tcpServer_(ioContext_, port, sessionManager_)
{
    channelManager_.AddChannel(1, "Channel 1", 100);
    channelManager_.AddChannel(2, "Channel 2", 100);
    channelManager_.AddChannel(3, "Channel 3", 100);
}

void ServerApplication::Run()
{
    tcpServer_.Start();

    std::cout << "Boost.Asio TCP server started"
              << " ioThreads=" << IO_THREAD_COUNT << '\n';

    for (const ChannelInfo& channel : channelManager_.GetChannelList())
    {
        std::cout << "Channel ready"
                  << " id=" << channel.id
                  << " name=" << channel.name
                  << " capacity=" << channel.maxPlayers << '\n';
    }

    std::vector<std::thread> ioThreads;
    ioThreads.reserve(IO_THREAD_COUNT);

    for (std::size_t index = 0; index < IO_THREAD_COUNT; ++index)
    {
        ioThreads.emplace_back(
            [this]
            {
                try
                {
                    ioContext_.run();
                }
                catch (const std::exception& exception)
                {
                    std::cerr << "I/O worker error: "
                              << exception.what() << '\n';
                }
            });
    }

    for (std::thread& thread : ioThreads)
    {
        thread.join();
    }
}
} // namespace dnf
