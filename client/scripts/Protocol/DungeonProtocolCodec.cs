using System;
using System.Collections.Generic;
using Dnf.Protocol;
using Google.FlatBuffers;

namespace DnfMockClient.Protocol;

public static class DungeonProtocolCodec
{
    public const ushort ProtocolVersion = 1;

    public static byte[] EncodeUdpHello(
        ulong dungeonId,
        ulong sessionId,
        ulong token)
    {
        ValidateIds(dungeonId, sessionId);
        if (token == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(token));
        }

        var builder = new FlatBufferBuilder(128);
        Offset<UdpHello> hello = UdpHello.CreateUdpHello(builder, sessionId, token);
        return FinishMessage(
            builder,
            dungeonId,
            DungeonPayload.UdpHello,
            hello.Value);
    }

    public static byte[] EncodeUdpHeartbeat(ulong dungeonId, ulong sessionId)
    {
        ValidateIds(dungeonId, sessionId);

        var builder = new FlatBufferBuilder(128);
        Offset<UdpHeartbeat> heartbeat =
            UdpHeartbeat.CreateUdpHeartbeat(builder, sessionId);
        return FinishMessage(
            builder,
            dungeonId,
            DungeonPayload.UdpHeartbeat,
            heartbeat.Value);
    }

    public static bool TryDecodeUdpHelloAck(
        byte[] bytes,
        out UdpHelloAckData? output)
    {
        output = null;

        if (bytes.Length == 0)
        {
            return false;
        }

        try
        {
            var buffer = new ByteBuffer(bytes);
            if (!DungeonMessage.DungeonMessageBufferHasIdentifier(buffer) ||
                !DungeonMessage.VerifyDungeonMessage(buffer))
            {
                return false;
            }

            DungeonMessage message = DungeonMessage.GetRootAsDungeonMessage(buffer);
            if (message.ProtocolVersion != ProtocolVersion ||
                message.DungeonId == 0 ||
                message.PayloadType != DungeonPayload.UdpHelloAck)
            {
                return false;
            }

            UdpHelloAck ack = message.PayloadAsUdpHelloAck();
            var result = (UdpHelloResult)(byte)ack.Result;
            if (!Enum.IsDefined(typeof(UdpHelloResult), result) ||
                (result != UdpHelloResult.Success && ack.ServerTick != 0))
            {
                return false;
            }

            output = new UdpHelloAckData(
                message.DungeonId,
                result,
                ack.ServerTick);
            return true;
        }
        catch (ArgumentOutOfRangeException)
        {
            return false;
        }
        catch (InvalidOperationException)
        {
            return false;
        }
    }

    public static byte[] EncodePlayerMovement(
        ulong dungeonId,
        uint sequence,
        float moveX,
        float moveY,
        bool jump)
    {
        ValidateDungeonId(dungeonId);
        ValidateDirection(moveX, moveY, allowZero: true);

        var builder = new FlatBufferBuilder(128);
        Offset<PlayerMovement> movement = PlayerMovement.CreatePlayerMovement(
            builder,
            sequence,
            moveX,
            moveY,
            jump);
        return FinishMessage(
            builder,
            dungeonId,
            DungeonPayload.PlayerMovement,
            movement.Value);
    }

    public static byte[] EncodePlayerAttack(
        ulong dungeonId,
        uint sequence,
        uint skillId,
        float directionX,
        float directionY)
    {
        ValidateDungeonId(dungeonId);
        if (skillId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(skillId));
        }

        ValidateDirection(directionX, directionY, allowZero: false);

        var builder = new FlatBufferBuilder(128);
        Offset<PlayerAttack> attack = PlayerAttack.CreatePlayerAttack(
            builder,
            sequence,
            skillId,
            directionX,
            directionY);
        return FinishMessage(
            builder,
            dungeonId,
            DungeonPayload.PlayerAttack,
            attack.Value);
    }

    public static bool TryDecodeSnapshot(
        byte[] bytes,
        out DungeonSnapshotData? output)
    {
        output = null;

        if (bytes.Length == 0)
        {
            return false;
        }

        try
        {
            var buffer = new ByteBuffer(bytes);
            if (!DungeonMessage.DungeonMessageBufferHasIdentifier(buffer) ||
                !DungeonMessage.VerifyDungeonMessage(buffer))
            {
                return false;
            }

            DungeonMessage message = DungeonMessage.GetRootAsDungeonMessage(buffer);
            if (message.ProtocolVersion != ProtocolVersion ||
                message.DungeonId == 0 ||
                message.PayloadType != DungeonPayload.DungeonSnapshot)
            {
                return false;
            }

            DungeonSnapshot snapshot = message.PayloadAsDungeonSnapshot();
            var players = new List<PlayerSnapshotData>(snapshot.PlayersLength);
            var enemies = new List<EnemySnapshotData>(snapshot.EnemiesLength);

            for (int index = 0; index < snapshot.PlayersLength; index++)
            {
                PlayerSnapshot? player = snapshot.Players(index);
                if (!player.HasValue || !player.Value.Position.HasValue)
                {
                    return false;
                }

                Vec3 position = player.Value.Position.Value;
                players.Add(new PlayerSnapshotData(
                    player.Value.SessionId,
                    player.Value.RoomId,
                    position.X,
                    position.Y,
                    position.Z));
            }

            for (int index = 0; index < snapshot.EnemiesLength; index++)
            {
                EnemySnapshot? enemy = snapshot.Enemies(index);
                if (!enemy.HasValue || !enemy.Value.Position.HasValue)
                {
                    return false;
                }

                Vec3 position = enemy.Value.Position.Value;
                enemies.Add(new EnemySnapshotData(
                    enemy.Value.EntityId,
                    enemy.Value.EnemyTemplateId,
                    enemy.Value.RoomId,
                    position.X,
                    position.Y,
                    position.Z,
                    enemy.Value.CurrentHp,
                    enemy.Value.Alive));
            }

            output = new DungeonSnapshotData(
                message.DungeonId,
                snapshot.ServerTick,
                players,
                enemies);
            return true;
        }
        catch (ArgumentOutOfRangeException)
        {
            return false;
        }
        catch (InvalidOperationException)
        {
            return false;
        }
    }

    private static byte[] FinishMessage(
        FlatBufferBuilder builder,
        ulong dungeonId,
        DungeonPayload payloadType,
        int payloadOffset)
    {
        Offset<DungeonMessage> message = DungeonMessage.CreateDungeonMessage(
            builder,
            ProtocolVersion,
            dungeonId,
            payloadType,
            payloadOffset);
        DungeonMessage.FinishDungeonMessageBuffer(builder, message);
        return builder.SizedByteArray();
    }

    private static void ValidateIds(ulong dungeonId, ulong sessionId)
    {
        ValidateDungeonId(dungeonId);
        if (sessionId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(sessionId));
        }
    }

    private static void ValidateDungeonId(ulong dungeonId)
    {
        if (dungeonId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(dungeonId));
        }
    }

    private static void ValidateDirection(float x, float y, bool allowZero)
    {
        bool inRange = float.IsFinite(x) && float.IsFinite(y) &&
            x is >= -1.0f and <= 1.0f && y is >= -1.0f and <= 1.0f;
        bool hasDirection = x != 0.0f || y != 0.0f;

        if (!inRange || (!allowZero && !hasDirection))
        {
            throw new ArgumentOutOfRangeException(nameof(x));
        }
    }
}
