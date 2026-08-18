#include "ChannelManager.h"

#include <cassert>
#include <iostream>

namespace
{
void TestChannelList()
{
    dnf::ChannelManager manager;

    assert(manager.AddChannel(2, "Channel 2", 20));
    assert(manager.AddChannel(1, "Channel 1", 10));
    assert(!manager.AddChannel(1, "Duplicate", 10));

    const auto channels = manager.GetChannelList();

    assert(channels.size() == 2);
    assert(channels[0].id == 1);
    assert(channels[0].maxPlayers == 10);
    assert(channels[1].id == 2);
}

void TestChannelCapacity()
{
    dnf::ChannelManager manager;
    manager.AddChannel(1, "Channel 1", 1);

    assert(manager.JoinChannel(100, 1) == dnf::JoinChannelResult::Success);
    assert(manager.JoinChannel(200, 1) == dnf::JoinChannelResult::ChannelFull);

    assert(manager.LeaveChannel(100));
    assert(manager.JoinChannel(200, 1) == dnf::JoinChannelResult::Success);
}

void TestOneChannelPerSession()
{
    dnf::ChannelManager manager;
    manager.AddChannel(1, "Channel 1", 10);
    manager.AddChannel(2, "Channel 2", 10);

    assert(manager.JoinChannel(100, 1) == dnf::JoinChannelResult::Success);
    assert(manager.JoinChannel(100, 2) == dnf::JoinChannelResult::AlreadyJoined);

    const auto joinedChannel = manager.GetJoinedChannel(100);
    assert(joinedChannel.has_value());
    assert(joinedChannel.value() == 1);
}

void TestUnknownChannel()
{
    dnf::ChannelManager manager;

    assert(manager.JoinChannel(100, 999) ==
           dnf::JoinChannelResult::ChannelNotFound);
    assert(!manager.LeaveChannel(100));
}
} // namespace

int main()
{
    TestChannelList();
    TestChannelCapacity();
    TestOneChannelPerSession();
    TestUnknownChannel();

    std::cout << "All channel manager tests passed.\n";
    return 0;
}
