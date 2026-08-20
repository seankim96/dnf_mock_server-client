using System.Collections.Generic;

namespace DnfMockClient.Protocol;

public enum DungeonStaticDataResult : byte
{
    Success = 0,
    DungeonNotFound = 1,
    NotDungeonParticipant = 2
}

public enum EnemyAiType : byte
{
    Melee = 0,
    Ranged = 1,
    Boss = 2
}

public sealed class StaticPositionData
{
    public StaticPositionData(float x, float y, float z)
    {
        X = x;
        Y = y;
        Z = z;
    }

    public float X { get; }
    public float Y { get; }
    public float Z { get; }
}

public sealed class StaticCollisionBoxData
{
    public StaticCollisionBoxData(
        StaticPositionData minimum,
        StaticPositionData maximum)
    {
        Minimum = minimum;
        Maximum = maximum;
    }

    public StaticPositionData Minimum { get; }
    public StaticPositionData Maximum { get; }
}

public sealed class PortalStaticData
{
    public PortalStaticData(
        uint portalId,
        StaticCollisionBoxData triggerArea,
        uint targetRoomId,
        StaticPositionData targetPosition,
        bool requiresRoomClear)
    {
        PortalId = portalId;
        TriggerArea = triggerArea;
        TargetRoomId = targetRoomId;
        TargetPosition = targetPosition;
        RequiresRoomClear = requiresRoomClear;
    }

    public uint PortalId { get; }
    public StaticCollisionBoxData TriggerArea { get; }
    public uint TargetRoomId { get; }
    public StaticPositionData TargetPosition { get; }
    public bool RequiresRoomClear { get; }
}

public sealed class ObstacleStaticData
{
    public ObstacleStaticData(
        uint obstacleId,
        StaticCollisionBoxData collision,
        bool destructible,
        uint maxHp)
    {
        ObstacleId = obstacleId;
        Collision = collision;
        Destructible = destructible;
        MaxHp = maxHp;
    }

    public uint ObstacleId { get; }
    public StaticCollisionBoxData Collision { get; }
    public bool Destructible { get; }
    public uint MaxHp { get; }
}

public sealed class EnemySpawnStaticData
{
    public EnemySpawnStaticData(
        uint enemySpawnId,
        uint enemyTemplateId,
        StaticPositionData position,
        uint wave)
    {
        EnemySpawnId = enemySpawnId;
        EnemyTemplateId = enemyTemplateId;
        Position = position;
        Wave = wave;
    }

    public uint EnemySpawnId { get; }
    public uint EnemyTemplateId { get; }
    public StaticPositionData Position { get; }
    public uint Wave { get; }
}

public sealed class RoomStaticData
{
    public RoomStaticData(
        uint roomId,
        float width,
        float depth,
        StaticPositionData playerSpawn,
        IReadOnlyList<PortalStaticData> portals,
        IReadOnlyList<ObstacleStaticData> obstacles,
        IReadOnlyList<EnemySpawnStaticData> enemySpawns)
    {
        RoomId = roomId;
        Width = width;
        Depth = depth;
        PlayerSpawn = playerSpawn;
        Portals = portals;
        Obstacles = obstacles;
        EnemySpawns = enemySpawns;
    }

    public uint RoomId { get; }
    public float Width { get; }
    public float Depth { get; }
    public StaticPositionData PlayerSpawn { get; }
    public IReadOnlyList<PortalStaticData> Portals { get; }
    public IReadOnlyList<ObstacleStaticData> Obstacles { get; }
    public IReadOnlyList<EnemySpawnStaticData> EnemySpawns { get; }
}

public sealed class EnemyTemplateStaticData
{
    public EnemyTemplateStaticData(
        uint enemyTemplateId,
        string displayName,
        uint maxHp,
        float moveSpeed,
        EnemyAiType aiType,
        StaticCollisionBoxData collision)
    {
        EnemyTemplateId = enemyTemplateId;
        DisplayName = displayName;
        MaxHp = maxHp;
        MoveSpeed = moveSpeed;
        AiType = aiType;
        Collision = collision;
    }

    public uint EnemyTemplateId { get; }
    public string DisplayName { get; }
    public uint MaxHp { get; }
    public float MoveSpeed { get; }
    public EnemyAiType AiType { get; }
    public StaticCollisionBoxData Collision { get; }
}

public sealed class DungeonStaticData
{
    public DungeonStaticData(
        DungeonStaticDataResult result,
        ulong dungeonId,
        uint dungeonTemplateId,
        IReadOnlyList<RoomStaticData> rooms,
        IReadOnlyList<EnemyTemplateStaticData> enemyTemplates)
    {
        Result = result;
        DungeonId = dungeonId;
        DungeonTemplateId = dungeonTemplateId;
        Rooms = rooms;
        EnemyTemplates = enemyTemplates;
    }

    public DungeonStaticDataResult Result { get; }
    public ulong DungeonId { get; }
    public uint DungeonTemplateId { get; }
    public IReadOnlyList<RoomStaticData> Rooms { get; }
    public IReadOnlyList<EnemyTemplateStaticData> EnemyTemplates { get; }
}
