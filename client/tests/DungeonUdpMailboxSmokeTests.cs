using DnfMockClient;
using DnfMockClient.Protocol;

internal static class DungeonUdpMailboxSmokeTests
{
    public static void Run()
    {
        KeepsOnlyTheLatestPendingValues();
        DrainClearsPendingValues();
    }

    private static void KeepsOnlyTheLatestPendingValues()
    {
        var mailbox = new DungeonUdpMailbox();
        mailbox.PublishSnapshot(Snapshot(10));
        mailbox.PublishSnapshot(Snapshot(11));
        mailbox.PublishError("first error");
        mailbox.PublishError("latest error");

        PendingDungeonUdpData pending = mailbox.Drain();

        Check(pending.Snapshot?.ServerTick == 11,
            "The mailbox did not keep the latest snapshot.");
        Check(pending.Error == "latest error",
            "The mailbox did not keep the latest UDP error.");
    }

    private static void DrainClearsPendingValues()
    {
        var mailbox = new DungeonUdpMailbox();
        mailbox.PublishSnapshot(Snapshot(20));
        mailbox.PublishError("error");

        mailbox.Drain();
        PendingDungeonUdpData empty = mailbox.Drain();

        Check(empty.Snapshot is null && empty.Error is null,
            "Drained UDP data remained pending.");
    }

    private static DungeonSnapshotData Snapshot(uint tick)
    {
        return new DungeonSnapshotData(
            1,
            tick,
            DungeonStateData.Running,
            Array.Empty<PlayerSnapshotData>(),
            Array.Empty<EnemySnapshotData>());
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
