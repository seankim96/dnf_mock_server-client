using System;
using System.Collections.Generic;
using System.IO;
using Google.FlatBuffers;
using TcpSchema = Dnf.Protocol.Tcp;

namespace DnfMockClient.Protocol;

public static class DungeonStaticDataCodec
{
    public static byte[] EncodeRequest(ulong dungeonId)
    {
        if (dungeonId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(dungeonId));
        }

        var builder = new FlatBufferBuilder(64);
        Offset<TcpSchema.DungeonStaticDataRequest> request =
            TcpSchema.DungeonStaticDataRequest.CreateDungeonStaticDataRequest(
                builder,
                dungeonId);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.DungeonStaticDataRequest,
            request.Value);
    }

    public static DungeonStaticData DecodeResponse(byte[] payload)
    {
        TcpSchema.DungeonStaticDataResponse response =
            TcpFlatBufferCodec.DecodePayload<
                TcpSchema.DungeonStaticDataResponse>(
                payload,
                TcpSchema.TcpPayload.DungeonStaticDataResponse);
        if (!Enum.IsDefined(
                typeof(TcpSchema.DungeonStaticDataResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid dungeon static data result.");
        }

        var rooms = new List<RoomStaticData>(response.RoomsLength);
        for (int roomIndex = 0; roomIndex < response.RoomsLength; roomIndex++)
        {
            TcpSchema.RoomStaticData? source = response.Rooms(roomIndex);
            if (!source.HasValue)
            {
                throw new InvalidDataException("Missing room static data.");
            }

            var portals = new List<PortalStaticData>(
                source.Value.PortalsLength);
            for (int index = 0; index < source.Value.PortalsLength; index++)
            {
                TcpSchema.PortalStaticData? portal =
                    source.Value.Portals(index);
                if (!portal.HasValue)
                {
                    throw new InvalidDataException("Missing portal static data.");
                }

                portals.Add(new PortalStaticData(
                    portal.Value.PortalId,
                    DecodeCollision(portal.Value.TriggerArea),
                    portal.Value.TargetRoomId,
                    DecodePosition(portal.Value.TargetPosition),
                    portal.Value.RequiresRoomClear));
            }

            var obstacles = new List<ObstacleStaticData>(
                source.Value.ObstaclesLength);
            for (int index = 0; index < source.Value.ObstaclesLength; index++)
            {
                TcpSchema.ObstacleStaticData? obstacle =
                    source.Value.Obstacles(index);
                if (!obstacle.HasValue)
                {
                    throw new InvalidDataException("Missing obstacle static data.");
                }

                obstacles.Add(new ObstacleStaticData(
                    obstacle.Value.ObstacleId,
                    DecodeCollision(obstacle.Value.Collision),
                    obstacle.Value.Destructible,
                    obstacle.Value.MaxHp));
            }

            var spawns = new List<EnemySpawnStaticData>(
                source.Value.EnemySpawnsLength);
            for (int index = 0; index < source.Value.EnemySpawnsLength; index++)
            {
                TcpSchema.EnemySpawnStaticData? spawn =
                    source.Value.EnemySpawns(index);
                if (!spawn.HasValue)
                {
                    throw new InvalidDataException("Missing enemy spawn data.");
                }

                spawns.Add(new EnemySpawnStaticData(
                    spawn.Value.EnemySpawnId,
                    spawn.Value.EnemyTemplateId,
                    DecodePosition(spawn.Value.Position),
                    spawn.Value.Wave));
            }

            rooms.Add(new RoomStaticData(
                source.Value.RoomId,
                source.Value.Width,
                source.Value.Depth,
                DecodePosition(source.Value.PlayerSpawn),
                portals,
                obstacles,
                spawns));
        }

        var enemies = new List<EnemyTemplateStaticData>(
            response.EnemyTemplatesLength);
        for (int index = 0; index < response.EnemyTemplatesLength; index++)
        {
            TcpSchema.EnemyTemplateStaticData? source =
                response.EnemyTemplates(index);
            if (!source.HasValue ||
                !Enum.IsDefined(typeof(TcpSchema.EnemyAiType), source.Value.AiType))
            {
                throw new InvalidDataException("Invalid enemy template data.");
            }

            enemies.Add(new EnemyTemplateStaticData(
                source.Value.EnemyTemplateId,
                source.Value.DisplayName,
                source.Value.MaxHp,
                source.Value.MoveSpeed,
                (EnemyAiType)(byte)source.Value.AiType,
                DecodeCollision(source.Value.Collision)));
        }

        var result = (DungeonStaticDataResult)(byte)response.Result;
        ValidateResponse(
            result,
            response.DungeonId,
            response.DungeonTemplateId,
            rooms,
            enemies);

        return new DungeonStaticData(
            result,
            response.DungeonId,
            response.DungeonTemplateId,
            rooms,
            enemies);
    }

    private static StaticPositionData DecodePosition(
        TcpSchema.StaticVec3? source)
    {
        if (!source.HasValue)
        {
            throw new InvalidDataException("Missing static position.");
        }

        return new StaticPositionData(
            source.Value.X,
            source.Value.Y,
            source.Value.Z);
    }

    private static StaticCollisionBoxData DecodeCollision(
        TcpSchema.StaticCollisionBox? source)
    {
        if (!source.HasValue)
        {
            throw new InvalidDataException("Missing static collision box.");
        }

        TcpSchema.StaticVec3 minimum = source.Value.Minimum;
        TcpSchema.StaticVec3 maximum = source.Value.Maximum;
        return new StaticCollisionBoxData(
            new StaticPositionData(minimum.X, minimum.Y, minimum.Z),
            new StaticPositionData(maximum.X, maximum.Y, maximum.Z));
    }

    private static void ValidateResponse(
        DungeonStaticDataResult result,
        ulong dungeonId,
        uint dungeonTemplateId,
        IReadOnlyList<RoomStaticData> rooms,
        IReadOnlyList<EnemyTemplateStaticData> enemies)
    {
        if (result != DungeonStaticDataResult.Success)
        {
            if (dungeonId != 0 || dungeonTemplateId != 0 ||
                rooms.Count != 0 || enemies.Count != 0)
            {
                throw new InvalidDataException("Failed static data must be empty.");
            }

            return;
        }

        if (dungeonId == 0 || dungeonTemplateId == 0 || rooms.Count == 0)
        {
            throw new InvalidDataException("Successful static data is incomplete.");
        }

        var enemyIds = new HashSet<uint>();
        foreach (EnemyTemplateStaticData enemy in enemies)
        {
            if (enemy.EnemyTemplateId == 0 ||
                !enemyIds.Add(enemy.EnemyTemplateId) ||
                string.IsNullOrEmpty(enemy.DisplayName) || enemy.MaxHp == 0 ||
                !float.IsFinite(enemy.MoveSpeed) || enemy.MoveSpeed < 0.0f ||
                !IsValidCollision(enemy.Collision) ||
                enemy.Collision.Minimum.Z < 0.0f)
            {
                throw new InvalidDataException("Invalid enemy template values.");
            }
        }

        var roomById = new Dictionary<uint, RoomStaticData>();
        foreach (RoomStaticData room in rooms)
        {
            if (room.RoomId == 0 || !float.IsFinite(room.Width) ||
                !float.IsFinite(room.Depth) || room.Width <= 0.0f ||
                room.Depth <= 0.0f || !IsInsideRoom(room, room.PlayerSpawn) ||
                !roomById.TryAdd(room.RoomId, room))
            {
                throw new InvalidDataException("Invalid room static values.");
            }
        }

        foreach (RoomStaticData room in rooms)
        {
            var portalIds = new HashSet<uint>();
            foreach (PortalStaticData portal in room.Portals)
            {
                if (portal.PortalId == 0 ||
                    !portalIds.Add(portal.PortalId) ||
                    !IsValidCollision(portal.TriggerArea) ||
                    portal.TriggerArea.Minimum.Z < 0.0f ||
                    !IsInsideRoom(room, portal.TriggerArea.Minimum) ||
                    !IsInsideRoom(room, portal.TriggerArea.Maximum) ||
                    !roomById.TryGetValue(
                        portal.TargetRoomId,
                        out RoomStaticData? targetRoom) ||
                    !IsInsideRoom(targetRoom, portal.TargetPosition))
                {
                    throw new InvalidDataException("Invalid portal static values.");
                }
            }

            var obstacleIds = new HashSet<uint>();
            foreach (ObstacleStaticData obstacle in room.Obstacles)
            {
                bool invalidHp =
                    obstacle.Destructible != (obstacle.MaxHp != 0);
                if (obstacle.ObstacleId == 0 ||
                    !obstacleIds.Add(obstacle.ObstacleId) ||
                    !IsValidCollision(obstacle.Collision) ||
                    obstacle.Collision.Minimum.Z < 0.0f ||
                    !IsInsideRoom(room, obstacle.Collision.Minimum) ||
                    !IsInsideRoom(room, obstacle.Collision.Maximum) ||
                    invalidHp)
                {
                    throw new InvalidDataException("Invalid obstacle values.");
                }
            }

            var spawnIds = new HashSet<uint>();
            foreach (EnemySpawnStaticData spawn in room.EnemySpawns)
            {
                if (spawn.EnemySpawnId == 0 ||
                    !spawnIds.Add(spawn.EnemySpawnId) ||
                    !enemyIds.Contains(spawn.EnemyTemplateId) ||
                    !IsInsideRoom(room, spawn.Position) || spawn.Wave == 0)
                {
                    throw new InvalidDataException("Invalid enemy spawn values.");
                }
            }
        }
    }

    private static bool IsInsideRoom(
        RoomStaticData room,
        StaticPositionData position)
    {
        return IsFinite(position) && position.X >= 0.0f &&
            position.X <= room.Width && position.Y >= 0.0f &&
            position.Y <= room.Depth && position.Z >= 0.0f;
    }

    private static bool IsValidCollision(StaticCollisionBoxData collision)
    {
        return IsFinite(collision.Minimum) && IsFinite(collision.Maximum) &&
            collision.Minimum.X < collision.Maximum.X &&
            collision.Minimum.Y < collision.Maximum.Y &&
            collision.Minimum.Z < collision.Maximum.Z;
    }

    private static bool IsFinite(StaticPositionData position)
    {
        return float.IsFinite(position.X) && float.IsFinite(position.Y) &&
            float.IsFinite(position.Z);
    }
}
