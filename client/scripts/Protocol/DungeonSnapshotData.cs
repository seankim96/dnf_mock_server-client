using System.Collections.Generic;

namespace DnfMockClient.Protocol;

public enum DungeonStateData
{
    Waiting,
    Running,
    Finished
}

public enum SkillPhaseData
{
    Idle,
    Startup,
    Active,
    Recovery
}

public sealed class SkillCooldownData
{
    public SkillCooldownData(uint skillId, uint remainingTicks)
    {
        SkillId = skillId;
        RemainingTicks = remainingTicks;
    }

    public uint SkillId { get; }
    public uint RemainingTicks { get; }
}

public sealed class PlayerSnapshotData
{
    public PlayerSnapshotData(
        ulong sessionId,
        uint roomId,
        float x,
        float y,
        float z,
        uint currentHp,
        uint maxHp,
        bool alive,
        uint currentMp,
        uint maxMp,
        uint skillId,
        SkillPhaseData skillPhase,
        uint skillRemainingTicks,
        IReadOnlyList<SkillCooldownData> cooldowns)
    {
        SessionId = sessionId;
        RoomId = roomId;
        X = x;
        Y = y;
        Z = z;
        CurrentHp = currentHp;
        MaxHp = maxHp;
        Alive = alive;
        CurrentMp = currentMp;
        MaxMp = maxMp;
        SkillId = skillId;
        SkillPhase = skillPhase;
        SkillRemainingTicks = skillRemainingTicks;
        Cooldowns = cooldowns;
    }

    public ulong SessionId { get; }
    public uint RoomId { get; }
    public float X { get; }
    public float Y { get; }
    public float Z { get; }
    public uint CurrentHp { get; }
    public uint MaxHp { get; }
    public bool Alive { get; }
    public uint CurrentMp { get; }
    public uint MaxMp { get; }
    public uint SkillId { get; }
    public SkillPhaseData SkillPhase { get; }
    public uint SkillRemainingTicks { get; }
    public IReadOnlyList<SkillCooldownData> Cooldowns { get; }

    public uint RemainingCooldown(uint skillId)
    {
        foreach (SkillCooldownData cooldown in Cooldowns)
        {
            if (cooldown.SkillId == skillId)
            {
                return cooldown.RemainingTicks;
            }
        }

        return 0;
    }
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
        DungeonStateData state,
        IReadOnlyList<PlayerSnapshotData> players,
        IReadOnlyList<EnemySnapshotData> enemies)
    {
        DungeonId = dungeonId;
        ServerTick = serverTick;
        State = state;
        Players = players;
        Enemies = enemies;
    }

    public ulong DungeonId { get; }
    public uint ServerTick { get; }
    public DungeonStateData State { get; }
    public IReadOnlyList<PlayerSnapshotData> Players { get; }
    public IReadOnlyList<EnemySnapshotData> Enemies { get; }
}
