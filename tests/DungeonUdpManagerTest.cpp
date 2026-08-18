#include "DungeonUdpManager.h"

#include <boost/asio/io_context.hpp>

#include <cassert>
#include <iostream>

namespace
{
void TestAllocateAndReleasePorts()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager manager(ioContext);

    const auto firstPort = manager.Allocate(100, {10, 20});
    const auto secondPort = manager.Allocate(200, {30});

    assert(firstPort.has_value());
    assert(secondPort.has_value());
    assert(firstPort.value() != 0);
    assert(secondPort.value() != 0);
    assert(firstPort.value() != secondPort.value());
    assert(manager.FindPort(100) == firstPort);
    assert(manager.FindPort(200) == secondPort);
    assert(manager.AllocationCount() == 2);

    const auto firstToken = manager.FindToken(100, 10);
    const auto secondToken = manager.FindToken(100, 20);
    assert(firstToken.has_value());
    assert(secondToken.has_value());
    assert(firstToken.value() != 0);
    assert(secondToken.value() != 0);
    assert(firstToken != secondToken);
    assert(!manager.FindToken(100, 999).has_value());

    assert(!manager.Allocate(100, {10}).has_value());
    assert(!manager.Allocate(0, {10}).has_value());
    assert(!manager.Allocate(300, {}).has_value());
    assert(!manager.Allocate(300, {10, 10}).has_value());

    assert(manager.Release(100));
    assert(!manager.FindPort(100).has_value());
    assert(!manager.FindToken(100, 10).has_value());
    assert(manager.AllocationCount() == 1);
    assert(!manager.Release(100));
}
} // namespace

int main()
{
    TestAllocateAndReleasePorts();

    std::cout << "All dungeon UDP manager tests passed.\n";
    return 0;
}
