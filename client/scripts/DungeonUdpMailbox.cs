using System;
using DnfMockClient.Protocol;

namespace DnfMockClient;

public readonly record struct PendingDungeonUdpData(
    DungeonSnapshotData? Snapshot,
    string? Error);

public sealed class DungeonUdpMailbox
{
    private readonly object _lock = new();
    private DungeonSnapshotData? _snapshot;
    private string? _error;

    public void PublishSnapshot(DungeonSnapshotData snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        lock (_lock)
        {
            _snapshot = snapshot;
        }
    }

    public void PublishError(string error)
    {
        ArgumentNullException.ThrowIfNull(error);

        lock (_lock)
        {
            _error = error;
        }
    }

    public PendingDungeonUdpData Drain()
    {
        lock (_lock)
        {
            var pending = new PendingDungeonUdpData(_snapshot, _error);
            _snapshot = null;
            _error = null;
            return pending;
        }
    }
}
