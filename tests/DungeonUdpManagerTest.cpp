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

    const auto firstPort = manager.Allocate(100);
    const auto secondPort = manager.Allocate(200);

    assert(firstPort.has_value());
    assert(secondPort.has_value());
    assert(firstPort.value() != 0);
    assert(secondPort.value() != 0);
    assert(firstPort.value() != secondPort.value());
    assert(manager.FindPort(100) == firstPort);
    assert(manager.FindPort(200) == secondPort);
    assert(manager.AllocationCount() == 2);

    assert(!manager.Allocate(100).has_value());
    assert(!manager.Allocate(0).has_value());

    assert(manager.Release(100));
    assert(!manager.FindPort(100).has_value());
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
