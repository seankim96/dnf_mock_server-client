using DnfMockClient;
using DnfMockClient.Protocol;

internal static class DungeonSnapshotBufferSmokeTests
{
    public static void Run()
    {
        InterpolatesMatchingRemotePlayer();
        DoesNotInterpolateAcrossRooms();
        AcceptsTickWrap();
        ResetClearsState();
    }

    private static void InterpolatesMatchingRemotePlayer()
    {
        var buffer = new DungeonSnapshotBuffer(30.0);
        buffer.Push(Snapshot(10, Player(7, 1, 0.0f, 0.0f)));
        buffer.Push(Snapshot(11, Player(7, 1, 30.0f, 60.0f)));

        buffer.Advance(1.0 / 60.0);
        SnapshotPosition sampled = buffer.SamplePlayer(buffer.Current!.Players[0]);

        Check(Near(sampled.X, 15.0f) && Near(sampled.Y, 30.0f),
            "A remote player was not sampled halfway between snapshots.");
    }

    private static void DoesNotInterpolateAcrossRooms()
    {
        var buffer = new DungeonSnapshotBuffer(30.0);
        buffer.Push(Snapshot(20, Player(7, 1, 10.0f, 10.0f)));
        buffer.Push(Snapshot(21, Player(7, 2, 90.0f, 80.0f)));

        SnapshotPosition sampled = buffer.SamplePlayer(buffer.Current!.Players[0]);
        Check(Near(sampled.X, 90.0f) && Near(sampled.Y, 80.0f),
            "A room transition must use the new room position immediately.");
    }

    private static void AcceptsTickWrap()
    {
        var buffer = new DungeonSnapshotBuffer(30.0);
        buffer.Push(Snapshot(uint.MaxValue, Player(7, 1, 0.0f, 0.0f)));
        buffer.Push(Snapshot(1, Player(7, 1, 60.0f, 0.0f)));

        buffer.Advance(1.0 / 30.0);
        SnapshotPosition sampled = buffer.SamplePlayer(buffer.Current!.Players[0]);
        Check(Near(sampled.X, 30.0f),
            "Tick wrap did not produce the expected two-tick interpolation.");
    }

    private static void ResetClearsState()
    {
        var buffer = new DungeonSnapshotBuffer(30.0);
        buffer.Push(Snapshot(1, Player(7, 1, 1.0f, 2.0f)));
        buffer.Reset();
        Check(buffer.Current is null, "Reset left a current snapshot behind.");
    }

    private static DungeonSnapshotData Snapshot(
        uint tick,
        PlayerSnapshotData player)
    {
        return new DungeonSnapshotData(
            1,
            tick,
            DungeonStateData.Running,
            new[] { player },
            Array.Empty<EnemySnapshotData>());
    }

    private static PlayerSnapshotData Player(
        ulong sessionId,
        uint roomId,
        float x,
        float y)
    {
        return new PlayerSnapshotData(
            sessionId,
            roomId,
            x,
            y,
            0.0f,
            100,
            100,
            true,
            100,
            100,
            0,
            SkillPhaseData.Idle,
            0,
            Array.Empty<SkillCooldownData>());
    }

    private static bool Near(float left, float right)
    {
        return MathF.Abs(left - right) < 0.001f;
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
