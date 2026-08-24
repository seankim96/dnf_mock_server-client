using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using Dnf.Protocol;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;
using Google.FlatBuffers;
using Xunit;
using static DnfMockClient.Tests.TestAssertions;
using static DnfMockClient.Tests.TestPayloads;
using AuthSchema = Dnf.Protocol.Auth;
using TcpSchema = Dnf.Protocol.Tcp;

namespace DnfMockClient.Tests;

internal static class TestPayloads
{
    internal static byte[] CreatePartySnapshotResponseBytes(
        TcpSchema.PartySnapshotResult result,
        ulong partyId,
        ulong leaderSessionId,
        ulong[] members)
    {
        var builder = new FlatBufferBuilder(128);
        VectorOffset memberIds =
            TcpSchema.PartySnapshotResponse.CreateMemberSessionIdsVector(
                builder,
                members);
        Offset<TcpSchema.PartySnapshotResponse> response =
            TcpSchema.PartySnapshotResponse.CreatePartySnapshotResponse(
                builder,
                result,
                partyId,
                leaderSessionId,
                memberIds);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.PartySnapshotResponse,
            response.Value);
    }

    internal static byte[] DungeonCatalogResponseBytes(
        TcpSchema.CatalogResult result,
        (uint Id, string Name, byte Recommended, byte Max, bool Available)[] dungeons)
    {
        var builder = new FlatBufferBuilder(256);
        var entries = new Offset<TcpSchema.DungeonCatalogEntry>[dungeons.Length];

        for (int index = 0; index < dungeons.Length; index++)
        {
            StringOffset name = builder.CreateString(dungeons[index].Name);
            entries[index] = TcpSchema.DungeonCatalogEntry
                .CreateDungeonCatalogEntry(
                    builder,
                    dungeons[index].Id,
                    name,
                    dungeons[index].Recommended,
                    dungeons[index].Max,
                    dungeons[index].Available);
        }

        VectorOffset dungeonEntries = TcpSchema.DungeonCatalogResponse
            .CreateDungeonsVector(builder, entries);
        Offset<TcpSchema.DungeonCatalogResponse> response =
            TcpSchema.DungeonCatalogResponse.CreateDungeonCatalogResponse(
                builder,
                result,
                dungeonEntries);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.DungeonCatalogResponse,
            response.Value);
    }

    internal static byte[] DungeonStaticDataResponseBytes(
        TcpSchema.DungeonStaticDataResult result,
        ulong dungeonId,
        uint dungeonTemplateId,
        uint spawnEnemyTemplateId = 2001)
    {
        var builder = new FlatBufferBuilder(1024);
        var roomEntries = Array.Empty<Offset<TcpSchema.RoomStaticData>>();
        var enemyEntries =
            Array.Empty<Offset<TcpSchema.EnemyTemplateStaticData>>();

        if (dungeonId != 0)
        {
            TcpSchema.PortalStaticData.StartPortalStaticData(builder);
            TcpSchema.PortalStaticData.AddPortalId(builder, 1);
            TcpSchema.PortalStaticData.AddTargetRoomId(builder, 2);
            TcpSchema.PortalStaticData.AddRequiresRoomClear(builder, true);
            Offset<TcpSchema.StaticVec3> targetPosition =
                TcpSchema.StaticVec3.CreateStaticVec3(
                    builder, 100.0f, 300.0f, 0.0f);
            TcpSchema.PortalStaticData.AddTargetPosition(builder, targetPosition);
            Offset<TcpSchema.StaticCollisionBox> triggerArea =
                TcpSchema.StaticCollisionBox.CreateStaticCollisionBox(
                    builder,
                    1100.0f, 200.0f, 0.0f,
                    1200.0f, 300.0f, 200.0f);
            TcpSchema.PortalStaticData.AddTriggerArea(builder, triggerArea);
            Offset<TcpSchema.PortalStaticData> portal =
                TcpSchema.PortalStaticData.EndPortalStaticData(builder);

            TcpSchema.ObstacleStaticData.StartObstacleStaticData(builder);
            TcpSchema.ObstacleStaticData.AddObstacleId(builder, 1);
            TcpSchema.ObstacleStaticData.AddDestructible(builder, true);
            TcpSchema.ObstacleStaticData.AddMaxHp(builder, 100);
            Offset<TcpSchema.StaticCollisionBox> obstacleCollision =
                TcpSchema.StaticCollisionBox.CreateStaticCollisionBox(
                    builder,
                    500.0f, 100.0f, 0.0f,
                    580.0f, 180.0f, 100.0f);
            TcpSchema.ObstacleStaticData.AddCollision(
                builder,
                obstacleCollision);
            Offset<TcpSchema.ObstacleStaticData> obstacle =
                TcpSchema.ObstacleStaticData.EndObstacleStaticData(builder);

            TcpSchema.EnemySpawnStaticData.StartEnemySpawnStaticData(builder);
            TcpSchema.EnemySpawnStaticData.AddEnemySpawnId(builder, 1);
            TcpSchema.EnemySpawnStaticData.AddEnemyTemplateId(
                builder,
                spawnEnemyTemplateId);
            TcpSchema.EnemySpawnStaticData.AddWave(builder, 1);
            Offset<TcpSchema.StaticVec3> enemyPosition =
                TcpSchema.StaticVec3.CreateStaticVec3(
                    builder, 800.0f, 250.0f, 0.0f);
            TcpSchema.EnemySpawnStaticData.AddPosition(builder, enemyPosition);
            Offset<TcpSchema.EnemySpawnStaticData> spawn =
                TcpSchema.EnemySpawnStaticData.EndEnemySpawnStaticData(builder);

            VectorOffset firstPortals = TcpSchema.RoomStaticData
                .CreatePortalsVector(builder, new[] { portal });
            VectorOffset firstObstacles = TcpSchema.RoomStaticData
                .CreateObstaclesVector(builder, new[] { obstacle });
            VectorOffset firstSpawns = TcpSchema.RoomStaticData
                .CreateEnemySpawnsVector(builder, new[] { spawn });
            TcpSchema.RoomStaticData.StartRoomStaticData(builder);
            TcpSchema.RoomStaticData.AddRoomId(builder, 1);
            TcpSchema.RoomStaticData.AddWidth(builder, 1200.0f);
            TcpSchema.RoomStaticData.AddDepth(builder, 500.0f);
            TcpSchema.RoomStaticData.AddPortals(builder, firstPortals);
            TcpSchema.RoomStaticData.AddObstacles(builder, firstObstacles);
            TcpSchema.RoomStaticData.AddEnemySpawns(builder, firstSpawns);
            Offset<TcpSchema.StaticVec3> firstPlayerSpawn =
                TcpSchema.StaticVec3.CreateStaticVec3(
                    builder, 100.0f, 250.0f, 0.0f);
            TcpSchema.RoomStaticData.AddPlayerSpawn(builder, firstPlayerSpawn);
            Offset<TcpSchema.RoomStaticData> firstRoom =
                TcpSchema.RoomStaticData.EndRoomStaticData(builder);

            VectorOffset secondPortals = TcpSchema.RoomStaticData
                .CreatePortalsVector(
                    builder,
                    Array.Empty<Offset<TcpSchema.PortalStaticData>>());
            VectorOffset secondObstacles = TcpSchema.RoomStaticData
                .CreateObstaclesVector(
                    builder,
                    Array.Empty<Offset<TcpSchema.ObstacleStaticData>>());
            VectorOffset secondSpawns = TcpSchema.RoomStaticData
                .CreateEnemySpawnsVector(
                    builder,
                    Array.Empty<Offset<TcpSchema.EnemySpawnStaticData>>());
            TcpSchema.RoomStaticData.StartRoomStaticData(builder);
            TcpSchema.RoomStaticData.AddRoomId(builder, 2);
            TcpSchema.RoomStaticData.AddWidth(builder, 1500.0f);
            TcpSchema.RoomStaticData.AddDepth(builder, 600.0f);
            TcpSchema.RoomStaticData.AddPortals(builder, secondPortals);
            TcpSchema.RoomStaticData.AddObstacles(builder, secondObstacles);
            TcpSchema.RoomStaticData.AddEnemySpawns(builder, secondSpawns);
            Offset<TcpSchema.StaticVec3> secondPlayerSpawn =
                TcpSchema.StaticVec3.CreateStaticVec3(
                    builder, 100.0f, 300.0f, 0.0f);
            TcpSchema.RoomStaticData.AddPlayerSpawn(builder, secondPlayerSpawn);
            Offset<TcpSchema.RoomStaticData> secondRoom =
                TcpSchema.RoomStaticData.EndRoomStaticData(builder);
            roomEntries = new[] { firstRoom, secondRoom };

            StringOffset enemyName = builder.CreateString("Goblin");
            TcpSchema.EnemyTemplateStaticData
                .StartEnemyTemplateStaticData(builder);
            TcpSchema.EnemyTemplateStaticData.AddEnemyTemplateId(builder, 2001);
            TcpSchema.EnemyTemplateStaticData.AddDisplayName(builder, enemyName);
            TcpSchema.EnemyTemplateStaticData.AddMaxHp(builder, 100);
            TcpSchema.EnemyTemplateStaticData.AddMoveSpeed(builder, 120.0f);
            TcpSchema.EnemyTemplateStaticData.AddAiType(
                builder,
                TcpSchema.EnemyAiType.Melee);
            Offset<TcpSchema.StaticCollisionBox> enemyCollision =
                TcpSchema.StaticCollisionBox.CreateStaticCollisionBox(
                    builder,
                    -20.0f, -15.0f, 0.0f,
                    20.0f, 15.0f, 80.0f);
            TcpSchema.EnemyTemplateStaticData.AddCollision(
                builder,
                enemyCollision);
            Offset<TcpSchema.EnemyTemplateStaticData> enemy =
                TcpSchema.EnemyTemplateStaticData
                    .EndEnemyTemplateStaticData(builder);
            enemyEntries = new[] { enemy };
        }

        VectorOffset rooms = TcpSchema.DungeonStaticDataResponse
            .CreateRoomsVector(builder, roomEntries);
        VectorOffset enemies = TcpSchema.DungeonStaticDataResponse
            .CreateEnemyTemplatesVector(builder, enemyEntries);
        Offset<TcpSchema.DungeonStaticDataResponse> response =
            TcpSchema.DungeonStaticDataResponse.CreateDungeonStaticDataResponse(
                builder,
                result,
                dungeonId,
                dungeonTemplateId,
                rooms,
                enemies);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.DungeonStaticDataResponse,
            response.Value);
    }

    internal static byte[] LoginResponseBytes(
        TcpSchema.LoginResult result,
        ulong sessionId)
    {
        var builder = new FlatBufferBuilder(64);
        Offset<TcpSchema.LoginResponse> response =
            TcpSchema.LoginResponse.CreateLoginResponse(
                builder,
                result,
                sessionId);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.LoginResponse,
            response.Value);
    }

    internal static byte[] ChannelListResponseBytes(
        (uint Id, string Name, uint CurrentPlayers, uint MaxPlayers)[] channels)
    {
        var builder = new FlatBufferBuilder(256);
        var entries = new Offset<TcpSchema.ChannelInfo>[channels.Length];

        for (int index = 0; index < channels.Length; index++)
        {
            StringOffset name = builder.CreateString(channels[index].Name);
            entries[index] = TcpSchema.ChannelInfo.CreateChannelInfo(
                builder,
                channels[index].Id,
                name,
                channels[index].CurrentPlayers,
                channels[index].MaxPlayers);
        }

        VectorOffset channelEntries =
            TcpSchema.ChannelListResponse.CreateChannelsVector(builder, entries);
        Offset<TcpSchema.ChannelListResponse> response =
            TcpSchema.ChannelListResponse.CreateChannelListResponse(
                builder,
                channelEntries);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.ChannelListResponse,
            response.Value);
    }

    internal static byte[] JoinChannelResponseBytes(
        TcpSchema.JoinChannelResult result,
        uint channelId)
    {
        var builder = new FlatBufferBuilder(64);
        Offset<TcpSchema.JoinChannelResponse> response =
            TcpSchema.JoinChannelResponse.CreateJoinChannelResponse(
                builder,
                result,
                channelId);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.JoinChannelResponse,
            response.Value);
    }

    internal static byte[] CreatePartyResponseBytes(
        TcpSchema.CreatePartyResult result,
        ulong partyId,
        ulong leaderSessionId)
    {
        var builder = new FlatBufferBuilder(128);
        Offset<TcpSchema.CreatePartyResponse> response =
            TcpSchema.CreatePartyResponse.CreateCreatePartyResponse(
                builder,
                result,
                partyId,
                leaderSessionId);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.CreatePartyResponse,
            response.Value);
    }

    internal static byte[] JoinPartyResponseBytes(
        TcpSchema.JoinPartyResult result,
        ulong partyId,
        ulong leaderSessionId)
    {
        var builder = new FlatBufferBuilder(128);
        Offset<TcpSchema.JoinPartyResponse> response =
            TcpSchema.JoinPartyResponse.CreateJoinPartyResponse(
                builder,
                result,
                partyId,
                leaderSessionId);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.JoinPartyResponse,
            response.Value);
    }

    internal static byte[] LeavePartyResponseBytes(TcpSchema.LeavePartyResult result)
    {
        var builder = new FlatBufferBuilder(64);
        Offset<TcpSchema.LeavePartyResponse> response =
            TcpSchema.LeavePartyResponse.CreateLeavePartyResponse(
                builder,
                result);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.LeavePartyResponse,
            response.Value);
    }

    internal static byte[] EnterDungeonResponseBytes(
        TcpSchema.EnterDungeonResult result,
        ulong dungeonId,
        ushort udpPort,
        ulong udpToken)
    {
        var builder = new FlatBufferBuilder(96);
        Offset<TcpSchema.EnterDungeonResponse> response =
            TcpSchema.EnterDungeonResponse.CreateEnterDungeonResponse(
                builder,
                result,
                dungeonId,
                udpPort,
                udpToken);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.EnterDungeonResponse,
            response.Value);
    }

    internal static byte[] DungeonConnectionInfoResponseBytes(
        TcpSchema.DungeonConnectionInfoResult result,
        ulong dungeonId,
        ushort udpPort,
        ulong udpToken)
    {
        var builder = new FlatBufferBuilder(96);
        Offset<TcpSchema.DungeonConnectionInfoResponse> response =
            TcpSchema.DungeonConnectionInfoResponse
                .CreateDungeonConnectionInfoResponse(
                    builder,
                    result,
                    dungeonId,
                    udpPort,
                    udpToken);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.DungeonConnectionInfoResponse,
            response.Value);
    }

    internal static byte[] CreateTestSnapshotBytes(
        uint currentMp = 80,
        uint maxMp = 100,
        uint currentHp = 90,
        uint maxHp = 100)
    {
        var builder = new FlatBufferBuilder(256);
        Offset<SkillCooldownSnapshot> cooldown =
            SkillCooldownSnapshot.CreateSkillCooldownSnapshot(
                builder,
                1001,
                30);
        VectorOffset cooldowns = PlayerSnapshot.CreateCooldownsVector(
            builder,
            new[] { cooldown });
        PlayerSnapshot.StartPlayerSnapshot(builder);
        PlayerSnapshot.AddSessionId(builder, 3);
        PlayerSnapshot.AddRoomId(builder, 1);
        Offset<Vec3> position = Vec3.CreateVec3(builder, 100.0f, 250.0f, 0.0f);
        PlayerSnapshot.AddPosition(builder, position);
        PlayerSnapshot.AddCurrentMp(builder, currentMp);
        PlayerSnapshot.AddMaxMp(builder, maxMp);
        PlayerSnapshot.AddCurrentHp(builder, currentHp);
        PlayerSnapshot.AddMaxHp(builder, maxHp);
        PlayerSnapshot.AddAlive(builder, currentHp > 0);
        PlayerSnapshot.AddSkillId(builder, 1001);
        PlayerSnapshot.AddSkillPhase(builder, SkillActionPhase.Active);
        PlayerSnapshot.AddSkillRemainingTicks(builder, 2);
        PlayerSnapshot.AddCooldowns(builder, cooldowns);
        Offset<PlayerSnapshot> player = PlayerSnapshot.EndPlayerSnapshot(builder);
        VectorOffset players = DungeonSnapshot.CreatePlayersVector(
            builder,
            new[] { player });
        VectorOffset enemies = DungeonSnapshot.CreateEnemiesVector(
            builder,
            Array.Empty<Offset<EnemySnapshot>>());
        Offset<DungeonSnapshot> snapshot = DungeonSnapshot.CreateDungeonSnapshot(
            builder,
            45,
            players,
            enemies,
            DungeonRunState.Running);
        Offset<DungeonMessage> snapshotMessage = DungeonMessage.CreateDungeonMessage(
            builder,
            DungeonProtocolCodec.ProtocolVersion,
            9,
            DungeonPayload.DungeonSnapshot,
            snapshot.Value);
        DungeonMessage.FinishDungeonMessageBuffer(builder, snapshotMessage);
        return builder.SizedByteArray();
    }

    internal static byte[] CreateUdpHelloAckBytes(
        UdpHelloAckResult result,
        uint serverTick)
    {
        var builder = new FlatBufferBuilder(128);
        Offset<UdpHelloAck> ack = UdpHelloAck.CreateUdpHelloAck(
            builder,
            result,
            serverTick);
        Offset<DungeonMessage> message = DungeonMessage.CreateDungeonMessage(
            builder,
            DungeonProtocolCodec.ProtocolVersion,
            9,
            DungeonPayload.UdpHelloAck,
            ack.Value);
        DungeonMessage.FinishDungeonMessageBuffer(builder, message);
        return builder.SizedByteArray();
    }

}
