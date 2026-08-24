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

public sealed class DungeonProtocolCodecTests
{
    [Fact]
    public void RoundTripsAndValidatesDungeonDatagrams()
    {
        byte[] helloBytes = DungeonProtocolCodec.EncodeUdpHello(9, 3, 77);
        var helloBuffer = new ByteBuffer(helloBytes);
        Assert(DungeonMessage.VerifyDungeonMessage(helloBuffer),
            "UDP hello FlatBuffer is invalid.");
        DungeonMessage helloMessage = DungeonMessage.GetRootAsDungeonMessage(helloBuffer);
        UdpHello hello = helloMessage.PayloadAsUdpHello();
        Assert(helloMessage.DungeonId == 9 && hello.SessionId == 3 && hello.Token == 77,
            "UDP hello values are incorrect.");

        byte[] ackBytes = CreateUdpHelloAckBytes(
            UdpHelloAckResult.Success,
            45);
        Assert(DungeonProtocolCodec.TryDecodeUdpHelloAck(
            ackBytes,
            out UdpHelloAckData? ack) && ack is not null,
            "UDP hello acknowledgement was not decoded.");
        Assert(ack!.DungeonId == 9 &&
            ack.Result == UdpHelloResult.Success &&
            ack.ServerTick == 45,
            "UDP hello acknowledgement values are incorrect.");

        byte[] invalidAckBytes = CreateUdpHelloAckBytes(
            UdpHelloAckResult.AuthenticationFailed,
            45);
        Assert(!DungeonProtocolCodec.TryDecodeUdpHelloAck(
            invalidAckBytes,
            out _),
            "Failed UDP hello acknowledgement with a server tick was accepted.");

        byte[] movementBytes = DungeonProtocolCodec.EncodePlayerMovement(
            9, 10, 0.5f, -0.25f, false);
        var movementBuffer = new ByteBuffer(movementBytes);
        DungeonMessage movementMessage =
            DungeonMessage.GetRootAsDungeonMessage(movementBuffer);
        PlayerMovement movement = movementMessage.PayloadAsPlayerMovement();
        Assert(movement.Sequence == 10 && movement.MoveX == 0.5f &&
            movement.MoveY == -0.25f, "Player movement values are incorrect.");

        byte[] snapshotBytes = CreateTestSnapshotBytes();

        Assert(DungeonProtocolCodec.TryDecodeSnapshot(
            snapshotBytes,
            out DungeonSnapshotData? decoded) && decoded is not null,
            "Dungeon snapshot was not decoded.");

        if (decoded is null)
        {
            throw new InvalidOperationException("Decoded snapshot is null.");
        }

        Assert(decoded.ServerTick == 45 && decoded.Players.Count == 1 &&
            decoded.State == DungeonStateData.Running &&
            decoded.Players[0].X == 100.0f && decoded.Players[0].Y == 250.0f &&
            decoded.Players[0].CurrentHp == 90 &&
            decoded.Players[0].MaxHp == 100 &&
            decoded.Players[0].Alive &&
            decoded.Players[0].CurrentMp == 80 &&
            decoded.Players[0].MaxMp == 100 &&
            decoded.Players[0].SkillId == 1001 &&
            decoded.Players[0].SkillPhase == SkillPhaseData.Active &&
            decoded.Players[0].SkillRemainingTicks == 2 &&
            decoded.Players[0].RemainingCooldown(1001) == 30,
            "Dungeon snapshot values are incorrect.");

        Assert(!DungeonProtocolCodec.TryDecodeSnapshot(
            CreateTestSnapshotBytes(101, 100),
            out _),
            "Dungeon snapshot with MP above the maximum was accepted.");
        Assert(!DungeonProtocolCodec.TryDecodeSnapshot(
            CreateTestSnapshotBytes(currentHp: 101, maxHp: 100),
            out _),
            "Dungeon snapshot with HP above the maximum was accepted.");
    }

}
