using System.Collections.Generic;

namespace DnfMockClient.Protocol;

public sealed class PlayerSnapshotData
{
    public PlayerSnapshotData(
        ulong sessionId,
        uint roomId,
        float x,
        float y,
        float z,
        uint currentMp,
        uint maxMp)
    {
        SessionId = sessionId;
        RoomId = roomId;
        X = x;
        Y = y;
        Z = z;
        CurrentMp = currentMp;
        MaxMp = maxMp;
    }

    public ulong SessionId { get; }
    public uint RoomId { get; }
    public float X { get; }
    public float Y { get; }
    public float Z { get; }
    public uint CurrentMp { get; }
    public uint MaxMp { get; }
}

public sealed class EnemySnapshotData
{
    public EnemySnapshotData(
        ulong entityId,
        uint enemyTemplateId,
        uint roomId,
        float x,
        float y,
        float z,
        uint currentHp,
        bool alive)
    {
        EntityId = entityId;
        EnemyTemplateId = enemyTemplateId;
        RoomId = roomId;
        X = x;
        Y = y;
        Z = z;
        CurrentHp = currentHp;
        Alive = alive;
    }

    public ulong EntityId { get; }
    public uint EnemyTemplateId { get; }
    public uint RoomId { get; }
    public float X { get; }
    public float Y { get; }
    public float Z { get; }
    public uint CurrentHp { get; }
    public bool Alive { get; }
}

public sealed class DungeonSnapshotData
{
    public DungeonSnapshotData(
        ulong dungeonId,
        uint serverTick,
        IReadOnlyList<PlayerSnapshotData> players,
        IReadOnlyList<EnemySnapshotData> enemies)
    {
        DungeonId = dungeonId;
        ServerTick = serverTick;
        Players = players;
        Enemies = enemies;
    }

    public ulong DungeonId { get; }
    public uint ServerTick { get; }
    public IReadOnlyList<PlayerSnapshotData> Players { get; }
    public IReadOnlyList<EnemySnapshotData> Enemies { get; }
}
