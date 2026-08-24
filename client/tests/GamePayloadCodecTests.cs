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

public sealed class GamePayloadCodecTests
{
    [Fact]
    public void RoundTripsAndValidatesGamePayloads()
    {
        byte[] loginPayload = GamePayloadCodec.EncodeLoginRequest("valid-ticket");
        var loginRequestBuffer = new ByteBuffer(loginPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(loginRequestBuffer),
            "Login request FlatBuffer is invalid.");
        TcpSchema.TcpMessage loginRequestMessage =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(loginRequestBuffer);
        Assert(loginRequestMessage.PayloadType == TcpSchema.TcpPayload.LoginRequest &&
            loginRequestMessage.PayloadAsLoginRequest().AuthTicket == "valid-ticket",
            "Login request payload is incorrect.");
        AssertThrows<ArgumentException>(
            () => GamePayloadCodec.EncodeLoginRequest(string.Empty),
            "An empty auth ticket was accepted.");
        AssertThrows<ArgumentException>(
            () => GamePayloadCodec.EncodeLoginRequest(new string('A', 257)),
            "An oversized auth ticket was accepted.");
        LoginResponseData login = GamePayloadCodec.DecodeLoginResponse(
            LoginResponseBytes(TcpSchema.LoginResult.Success, 42));
        Assert(login.Result == LoginResult.Success && login.SessionId == 42,
            "Login response payload is incorrect.");

        LoginResponseData failedLogin = GamePayloadCodec.DecodeLoginResponse(
            LoginResponseBytes(TcpSchema.LoginResult.InvalidTicket, 0));
        Assert(failedLogin.Result == LoginResult.InvalidTicket &&
            failedLogin.SessionId == 0, "Failed login payload is incorrect.");

        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeLoginResponse(
                LoginResponseBytes(TcpSchema.LoginResult.Success, 0)),
            "Successful login with a zero session ID was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeLoginResponse(
                LoginResponseBytes(TcpSchema.LoginResult.InvalidTicket, 42)),
            "Failed login with a non-zero session ID was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeLoginResponse(
                LoginResponseBytes((TcpSchema.LoginResult)byte.MaxValue, 0)),
            "Unknown login result was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeLoginResponse(loginPayload),
            "Login request was accepted as a response.");

        byte[] channelListRequest =
            GamePayloadCodec.EncodeChannelListRequest();
        var channelListRequestBuffer = new ByteBuffer(channelListRequest);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(channelListRequestBuffer),
            "Channel list request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(channelListRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.ChannelListRequest,
            "Channel list request type is incorrect.");

        IReadOnlyList<ChannelInfo> channels =
            GamePayloadCodec.DecodeChannelListResponse(
                ChannelListResponseBytes(new[]
                {
                    (2U, "Channel 2", 5U, 100U),
                    (3U, "Channel 3", 0U, 200U)
                }));
        Assert(channels.Count == 2, "Channel count is incorrect.");
        Assert(channels[0].Id == 2 &&
            channels[0].DisplayName == "Channel 2" &&
            channels[0].CurrentPlayers == 5 && channels[0].MaxPlayers == 100,
            "Channel entry is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeChannelListResponse(channelListRequest),
            "Channel list request was accepted as a response.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeChannelListResponse(
                ChannelListResponseBytes(new[]
                {
                    (0U, "Invalid", 0U, 100U)
                })),
            "Channel list entry with a zero ID was accepted.");

        byte[] joinPayload = GamePayloadCodec.EncodeJoinChannelRequest(3);
        var joinChannelRequestBuffer = new ByteBuffer(joinPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(joinChannelRequestBuffer),
            "Join channel request FlatBuffer is invalid.");
        TcpSchema.TcpMessage joinChannelRequestMessage =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(joinChannelRequestBuffer);
        Assert(joinChannelRequestMessage.PayloadType ==
            TcpSchema.TcpPayload.JoinChannelRequest &&
            joinChannelRequestMessage.PayloadAsJoinChannelRequest().ChannelId == 3,
            "Join channel request payload is incorrect.");
        AssertThrows<ArgumentOutOfRangeException>(
            () => GamePayloadCodec.EncodeJoinChannelRequest(0),
            "Join channel request accepted a zero channel ID.");
        JoinChannelResponse joinResponse = GamePayloadCodec.DecodeJoinChannelResponse(
            JoinChannelResponseBytes(
                TcpSchema.JoinChannelResult.Success,
                3));
        Assert(joinResponse.Result == JoinChannelResult.Success &&
            joinResponse.ChannelId == 3, "Join channel response is incorrect.");
        JoinChannelResponse missingChannel =
            GamePayloadCodec.DecodeJoinChannelResponse(
                JoinChannelResponseBytes(
                    TcpSchema.JoinChannelResult.ChannelNotFound,
                    0));
        Assert(missingChannel.Result == JoinChannelResult.ChannelNotFound &&
            missingChannel.ChannelId == 0,
            "Failed join channel response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeJoinChannelResponse(
                JoinChannelResponseBytes(
                    TcpSchema.JoinChannelResult.Success,
                    0)),
            "Successful join channel response with a zero ID was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeJoinChannelResponse(
                JoinChannelResponseBytes(
                    TcpSchema.JoinChannelResult.ChannelFull,
                    3)),
            "Failed join channel response with a channel ID was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeJoinChannelResponse(
                JoinChannelResponseBytes(
                    (TcpSchema.JoinChannelResult)4,
                    0)),
            "Unknown join channel result was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeJoinChannelResponse(joinPayload),
            "Join channel request was accepted as a response.");

        byte[] createPartyRequest = GamePayloadCodec.EncodeCreatePartyRequest();
        var createPartyRequestBuffer = new ByteBuffer(createPartyRequest);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(createPartyRequestBuffer),
            "Create party request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(createPartyRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.CreatePartyRequest,
            "Create party request type is incorrect.");

        CreatePartyResponse createParty = GamePayloadCodec.DecodeCreatePartyResponse(
            CreatePartyResponseBytes(
                TcpSchema.CreatePartyResult.Success,
                9,
                42));
        Assert(createParty.Result == CreatePartyResult.Success &&
            createParty.PartyId == 9 && createParty.LeaderSessionId == 42,
            "Create party response is incorrect.");
        CreatePartyResponse failedParty = GamePayloadCodec.DecodeCreatePartyResponse(
            CreatePartyResponseBytes(
                TcpSchema.CreatePartyResult.AlreadyInParty,
                0,
                0));
        Assert(failedParty.Result == CreatePartyResult.AlreadyInParty &&
            failedParty.PartyId == 0 && failedParty.LeaderSessionId == 0,
            "Failed create party response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeCreatePartyResponse(
                CreatePartyResponseBytes(
                    TcpSchema.CreatePartyResult.Success,
                    0,
                    0)),
            "Successful create party response with zero IDs was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeCreatePartyResponse(createPartyRequest),
            "Create party request was accepted as a response.");

        byte[] joinPartyPayload = GamePayloadCodec.EncodeJoinPartyRequest(9);
        var joinPartyRequestBuffer = new ByteBuffer(joinPartyPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(joinPartyRequestBuffer),
            "Join party request FlatBuffer is invalid.");
        TcpSchema.TcpMessage joinPartyRequestMessage =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(joinPartyRequestBuffer);
        Assert(joinPartyRequestMessage.PayloadType ==
            TcpSchema.TcpPayload.JoinPartyRequest &&
            joinPartyRequestMessage.PayloadAsJoinPartyRequest().PartyId == 9,
            "Join party request payload is incorrect.");

        JoinPartyResponse joinParty = GamePayloadCodec.DecodeJoinPartyResponse(
            JoinPartyResponseBytes(
                TcpSchema.JoinPartyResult.Success,
                9,
                42));
        Assert(joinParty.Result == JoinPartyResult.Success &&
            joinParty.PartyId == 9 && joinParty.LeaderSessionId == 42,
            "Join party response is incorrect.");
        JoinPartyResponse missingParty = GamePayloadCodec.DecodeJoinPartyResponse(
            JoinPartyResponseBytes(
                TcpSchema.JoinPartyResult.PartyNotFound,
                0,
                0));
        Assert(missingParty.Result == JoinPartyResult.PartyNotFound &&
            missingParty.PartyId == 0 && missingParty.LeaderSessionId == 0,
            "Failed join party response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeJoinPartyResponse(joinPartyPayload),
            "Join party request was accepted as a response.");

        byte[] leavePartyRequest = GamePayloadCodec.EncodeLeavePartyRequest();
        var leavePartyRequestBuffer = new ByteBuffer(leavePartyRequest);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(leavePartyRequestBuffer),
            "Leave party request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(leavePartyRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.LeavePartyRequest,
            "Leave party request type is incorrect.");
        Assert(GamePayloadCodec.DecodeLeavePartyResponse(
                LeavePartyResponseBytes(TcpSchema.LeavePartyResult.Success)) ==
            LeavePartyResult.Success, "Leave party success result is incorrect.");
        Assert(GamePayloadCodec.DecodeLeavePartyResponse(
                LeavePartyResponseBytes(TcpSchema.LeavePartyResult.NotInParty)) ==
            LeavePartyResult.NotInParty, "Leave party failure result is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeLeavePartyResponse(
                LeavePartyResponseBytes((TcpSchema.LeavePartyResult)2)),
            "Unknown leave party result was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeLeavePartyResponse(leavePartyRequest),
            "Leave party request was accepted as a response.");

        byte[] partySnapshotRequest =
            GamePayloadCodec.EncodePartySnapshotRequest();
        var partySnapshotRequestBuffer = new ByteBuffer(partySnapshotRequest);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(partySnapshotRequestBuffer),
            "Party snapshot request FlatBuffer is invalid.");
        TcpSchema.TcpMessage partySnapshotRequestMessage =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(partySnapshotRequestBuffer);
        Assert(partySnapshotRequestMessage.PayloadType ==
            TcpSchema.TcpPayload.PartySnapshotRequest,
            "Party snapshot request type is incorrect.");

        PartySnapshotData partySnapshot =
            GamePayloadCodec.DecodePartySnapshotResponse(
                CreatePartySnapshotResponseBytes(
                    TcpSchema.PartySnapshotResult.Success,
                    9,
                    42,
                    new ulong[] { 42, 43 }));
        Assert(partySnapshot.Result == PartySnapshotResult.Success &&
            partySnapshot.PartyId == 9 && partySnapshot.LeaderSessionId == 42 &&
            partySnapshot.Members.SequenceEqual(new ulong[] { 42, 43 }),
            "Party snapshot response is incorrect.");
        PartySnapshotData noPartySnapshot =
            GamePayloadCodec.DecodePartySnapshotResponse(
                CreatePartySnapshotResponseBytes(
                    TcpSchema.PartySnapshotResult.NotInParty,
                    0,
                    0,
                    Array.Empty<ulong>()));
        Assert(noPartySnapshot.Result == PartySnapshotResult.NotInParty &&
            noPartySnapshot.Members.Count == 0,
            "Not-in-party snapshot response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodePartySnapshotResponse(
                CreatePartySnapshotResponseBytes(
                    TcpSchema.PartySnapshotResult.Success,
                    9,
                    42,
                    new ulong[] { 43 })),
            "Party snapshot without the leader was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodePartySnapshotResponse(
                partySnapshotRequest),
            "Party snapshot request was accepted as a response.");

        byte[] dungeonCatalogRequest =
            GamePayloadCodec.EncodeDungeonCatalogRequest();
        var dungeonCatalogRequestBuffer = new ByteBuffer(dungeonCatalogRequest);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(dungeonCatalogRequestBuffer),
            "Dungeon catalog request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(
                dungeonCatalogRequestBuffer).PayloadType ==
            TcpSchema.TcpPayload.DungeonCatalogRequest,
            "Dungeon catalog request type is incorrect.");

        DungeonCatalogData dungeonCatalog =
            GamePayloadCodec.DecodeDungeonCatalogResponse(
                DungeonCatalogResponseBytes(
                    TcpSchema.CatalogResult.Success,
                    new[]
                    {
                        (1000U, "Training Room", (byte)1, (byte)1, true),
                        (1001U, "Forest", (byte)2, (byte)4, true)
                    }));
        Assert(dungeonCatalog.Result == CatalogResult.Success &&
            dungeonCatalog.Dungeons.Count == 2 &&
            dungeonCatalog.Dungeons[1].TemplateId == 1001 &&
            dungeonCatalog.Dungeons[1].DisplayName == "Forest" &&
            dungeonCatalog.Dungeons[1].RecommendedPartySize == 2 &&
            dungeonCatalog.Dungeons[1].MaxPartySize == 4 &&
            dungeonCatalog.Dungeons[1].Available,
            "Dungeon catalog response is incorrect.");

        DungeonCatalogData unavailableCatalog =
            GamePayloadCodec.DecodeDungeonCatalogResponse(
                DungeonCatalogResponseBytes(
                    TcpSchema.CatalogResult.Unavailable,
                    Array.Empty<(uint, string, byte, byte, bool)>()));
        Assert(unavailableCatalog.Result == CatalogResult.Unavailable &&
            unavailableCatalog.Dungeons.Count == 0,
            "Unavailable dungeon catalog response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeDungeonCatalogResponse(
                DungeonCatalogResponseBytes(
                    TcpSchema.CatalogResult.Success,
                    new[]
                    {
                        (0U, "Invalid", (byte)1, (byte)4, true)
                    })),
            "Dungeon catalog entry with a zero ID was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeDungeonCatalogResponse(
                DungeonCatalogResponseBytes(
                    (TcpSchema.CatalogResult)2,
                    Array.Empty<(uint, string, byte, byte, bool)>())),
            "Unknown dungeon catalog result was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeDungeonCatalogResponse(
                dungeonCatalogRequest),
            "Dungeon catalog request was accepted as a response.");

        byte[] staticDataRequest = DungeonStaticDataCodec.EncodeRequest(5001);
        var staticDataRequestBuffer = new ByteBuffer(staticDataRequest);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(staticDataRequestBuffer),
            "Dungeon static data request FlatBuffer is invalid.");
        TcpSchema.TcpMessage staticDataRequestMessage =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(staticDataRequestBuffer);
        Assert(staticDataRequestMessage.PayloadType ==
            TcpSchema.TcpPayload.DungeonStaticDataRequest &&
            staticDataRequestMessage.PayloadAsDungeonStaticDataRequest()
                .DungeonId == 5001,
            "Dungeon static data request is incorrect.");
        AssertThrows<ArgumentOutOfRangeException>(
            () => DungeonStaticDataCodec.EncodeRequest(0),
            "Dungeon static data request accepted a zero ID.");

        DungeonStaticData staticData = DungeonStaticDataCodec.DecodeResponse(
            DungeonStaticDataResponseBytes(
                TcpSchema.DungeonStaticDataResult.Success,
                5001,
                1001));
        Assert(staticData.Result == DungeonStaticDataResult.Success &&
            staticData.DungeonId == 5001 &&
            staticData.DungeonTemplateId == 1001 &&
            staticData.Rooms.Count == 2 &&
            staticData.Rooms[0].Portals[0].TargetRoomId == 2 &&
            staticData.Rooms[0].Obstacles[0].Destructible &&
            staticData.Rooms[0].EnemySpawns[0].EnemyTemplateId == 2001 &&
            staticData.EnemyTemplates.Count == 1 &&
            staticData.EnemyTemplates[0].DisplayName == "Goblin",
            "Dungeon static data response is incorrect.");

        DungeonStaticData missingStaticData =
            DungeonStaticDataCodec.DecodeResponse(
                DungeonStaticDataResponseBytes(
                    TcpSchema.DungeonStaticDataResult.DungeonNotFound,
                    0,
                    0));
        Assert(missingStaticData.Result ==
            DungeonStaticDataResult.DungeonNotFound &&
            missingStaticData.Rooms.Count == 0,
            "Failed dungeon static data response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => DungeonStaticDataCodec.DecodeResponse(
                DungeonStaticDataResponseBytes(
                    TcpSchema.DungeonStaticDataResult.Success,
                    0,
                    0)),
            "Incomplete successful static data was accepted.");
        AssertThrows<InvalidDataException>(
            () => DungeonStaticDataCodec.DecodeResponse(
                DungeonStaticDataResponseBytes(
                    TcpSchema.DungeonStaticDataResult.Success,
                    5001,
                    1001,
                    9999)),
            "Static data with a missing enemy template was accepted.");
        AssertThrows<InvalidDataException>(
            () => DungeonStaticDataCodec.DecodeResponse(
                DungeonStaticDataResponseBytes(
                    (TcpSchema.DungeonStaticDataResult)3,
                    0,
                    0)),
            "Unknown dungeon static data result was accepted.");
        AssertThrows<InvalidDataException>(
            () => DungeonStaticDataCodec.DecodeResponse(staticDataRequest),
            "Dungeon static data request was accepted as a response.");

        byte[] enterDungeonRequest =
            GamePayloadCodec.EncodeEnterDungeonRequest(1001);
        var enterDungeonRequestBuffer = new ByteBuffer(enterDungeonRequest);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(enterDungeonRequestBuffer),
            "Enter dungeon request FlatBuffer is invalid.");
        TcpSchema.TcpMessage enterDungeonRequestMessage =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(enterDungeonRequestBuffer);
        Assert(enterDungeonRequestMessage.PayloadType ==
            TcpSchema.TcpPayload.EnterDungeonRequest &&
            enterDungeonRequestMessage.PayloadAsEnterDungeonRequest()
                .DungeonTemplateId == 1001,
            "Enter dungeon request payload is incorrect.");
        AssertThrows<ArgumentOutOfRangeException>(
            () => GamePayloadCodec.EncodeEnterDungeonRequest(0),
            "Enter dungeon request accepted a zero template ID.");

        EnterDungeonResponse dungeonResponse =
            GamePayloadCodec.DecodeEnterDungeonResponse(
                EnterDungeonResponseBytes(
                    TcpSchema.EnterDungeonResult.Success,
                    9,
                    0x2345,
                    7));
        Assert(dungeonResponse.Result == EnterDungeonResult.Success &&
            dungeonResponse.DungeonId == 9 && dungeonResponse.UdpPort == 0x2345 &&
            dungeonResponse.UdpToken == 7, "Dungeon response is incorrect.");
        EnterDungeonResponse failedDungeon =
            GamePayloadCodec.DecodeEnterDungeonResponse(
                EnterDungeonResponseBytes(
                    TcpSchema.EnterDungeonResult.NotInParty,
                    0,
                    0,
                    0));
        Assert(failedDungeon.Result == EnterDungeonResult.NotInParty &&
            failedDungeon.DungeonId == 0 && failedDungeon.UdpPort == 0 &&
            failedDungeon.UdpToken == 0,
            "Failed dungeon response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeEnterDungeonResponse(
                EnterDungeonResponseBytes(
                    TcpSchema.EnterDungeonResult.Success,
                    0,
                    0x2345,
                    7)),
            "Successful dungeon response with a zero ID was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeEnterDungeonResponse(
                EnterDungeonResponseBytes(
                    TcpSchema.EnterDungeonResult.NotPartyLeader,
                    9,
                    0x2345,
                    7)),
            "Failed dungeon response with connection data was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeEnterDungeonResponse(
                EnterDungeonResponseBytes(
                    (TcpSchema.EnterDungeonResult)6,
                    0,
                    0,
                    0)),
            "Unknown enter dungeon result was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeEnterDungeonResponse(enterDungeonRequest),
            "Enter dungeon request was accepted as a response.");

        byte[] connectionInfoRequest =
            GamePayloadCodec.EncodeDungeonConnectionInfoRequest();
        var connectionInfoRequestBuffer = new ByteBuffer(connectionInfoRequest);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(connectionInfoRequestBuffer),
            "Dungeon connection info request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(connectionInfoRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.DungeonConnectionInfoRequest,
            "Dungeon connection info request type is incorrect.");

        DungeonConnectionInfo connectionInfo =
            GamePayloadCodec.DecodeDungeonConnectionInfoResponse(
                DungeonConnectionInfoResponseBytes(
                    TcpSchema.DungeonConnectionInfoResult.Success,
                    9,
                    0x2345,
                    8));
        Assert(connectionInfo.Result == DungeonConnectionInfoResult.Success &&
            connectionInfo.DungeonId == 9 && connectionInfo.UdpPort == 0x2345 &&
            connectionInfo.UdpToken == 8,
            "Dungeon connection info response is incorrect.");
        DungeonConnectionInfo missingDungeon =
            GamePayloadCodec.DecodeDungeonConnectionInfoResponse(
                DungeonConnectionInfoResponseBytes(
                    TcpSchema.DungeonConnectionInfoResult.DungeonNotFound,
                    0,
                    0,
                    0));
        Assert(missingDungeon.Result == DungeonConnectionInfoResult.DungeonNotFound &&
            missingDungeon.DungeonId == 0 && missingDungeon.UdpPort == 0 &&
            missingDungeon.UdpToken == 0,
            "Failed dungeon connection info response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeDungeonConnectionInfoResponse(
                DungeonConnectionInfoResponseBytes(
                    TcpSchema.DungeonConnectionInfoResult.Success,
                    0,
                    0x2345,
                    8)),
            "Successful connection info response with a zero ID was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeDungeonConnectionInfoResponse(
                DungeonConnectionInfoResponseBytes(
                    (TcpSchema.DungeonConnectionInfoResult)5,
                    0,
                    0,
                    0)),
            "Unknown dungeon connection info result was accepted.");
        AssertThrows<InvalidDataException>(
            () => GamePayloadCodec.DecodeDungeonConnectionInfoResponse(
                connectionInfoRequest),
            "Dungeon connection info request was accepted as a response.");
    }

}
