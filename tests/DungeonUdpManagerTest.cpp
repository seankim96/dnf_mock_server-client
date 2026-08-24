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

void TestLatestPendingSnapshotAndOversizeStats()
{
    boost::asio::io_context ioContext;
    dnf::DungeonUdpManager manager(ioContext);

    assert(manager.Allocate(300, {40}).has_value());
    assert(!manager.FindStats(999).has_value());

    assert(!manager.BroadcastSnapshot(
        300,
        std::vector<std::uint8_t>(
            dnf::MAX_DUNGEON_DATAGRAM_SIZE + 1,
            0)));

    assert(manager.BroadcastSnapshot(300, {1}));
    assert(manager.BroadcastSnapshot(300, {2}));
    assert(manager.BroadcastSnapshot(300, {3}));

    const auto pendingStats = manager.FindStats(300);
    assert(pendingStats.has_value());
    assert(pendingStats->acceptedSnapshotCount == 3);
    assert(pendingStats->replacedSnapshotCount == 2);
    assert(pendingStats->oversizedSnapshotCount == 1);
    assert(pendingStats->snapshotPending);
    assert(!pendingStats->snapshotSendInProgress);

    ioContext.poll();

    const auto drainedStats = manager.FindStats(300);
    assert(drainedStats.has_value());
    assert(drainedStats->acceptedSnapshotCount == 3);
    assert(drainedStats->replacedSnapshotCount == 2);
    assert(drainedStats->sentSnapshotDatagramCount == 0);
    assert(!drainedStats->snapshotPending);
    assert(!drainedStats->snapshotSendInProgress);

    assert(manager.Release(300));
    ioContext.restart();
    ioContext.poll();
}
} // namespace

int main()
{
    TestAllocateAndReleasePorts();
    TestLatestPendingSnapshotAndOversizeStats();

    std::cout << "All dungeon UDP manager tests passed.\n";
    return 0;
}
