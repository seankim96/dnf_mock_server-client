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
using AuthSchema = Dnf.Protocol.Auth;
using TcpSchema = Dnf.Protocol.Tcp;

TestHeaderEncoding();
TestSplitPacket();
TestCombinedPackets();
TestInvalidPacketSize();
TestGamePayloads();
TestTcpFlatBufferSchema();
TestAuthFlatBufferSchema();
AuthPayloadCodecSmokeTests.Run();
TestDungeonProtocol();
await TestTcpConnectionAsync();
await TestAuthTlsConnectionAsync();
await TestUdpSessionAsync();

Console.WriteLine("All client smoke tests passed.");

static void TestHeaderEncoding()
{
    var header = new TcpPacketHeader(
        10,
        TcpPacketType.LoginRequest,
        0x01020304);

    byte[] encoded = TcpPacketCodec.EncodeHeader(header);
    byte[] expected = { 0, 10, 0, 1, 1, 2, 3, 4 };

    Assert(encoded.SequenceEqual(expected), "Header must use big-endian byte order.");

    TcpPacketHeader decoded = TcpPacketCodec.DecodeHeader(encoded);
    Assert(decoded.PacketSize == 10, "Packet size was decoded incorrectly.");
    Assert(decoded.Type == TcpPacketType.LoginRequest, "Packet type was decoded incorrectly.");
    Assert(decoded.RequestId == 0x01020304, "Request ID was decoded incorrectly.");
}

static void TestSplitPacket()
{
    byte[] packetBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.LoginRequest,
        1,
        new byte[] { (byte)'O', (byte)'K' });

    var buffer = new TcpReceiveBuffer();
    buffer.Append(packetBytes[..5]);

    Assert(!buffer.TryPop(out _), "An incomplete packet must not be popped.");

    buffer.Append(packetBytes[5..]);
    if (!buffer.TryPop(out TcpPacket? packet) || packet is null)
    {
        throw new InvalidOperationException("A completed packet was not popped.");
    }

    Assert(packet.Header.RequestId == 1, "Split packet request ID is incorrect.");
    Assert(packet.Payload.SequenceEqual(new byte[] { (byte)'O', (byte)'K' }),
        "Split packet payload is incorrect.");
    Assert(buffer.Size == 0, "The popped packet must be removed from the buffer.");
}

static void TestCombinedPackets()
{
    byte[] first = TcpPacketCodec.EncodePacket(
        TcpPacketType.ChannelListRequest,
        10,
        Array.Empty<byte>());
    byte[] second = TcpPacketCodec.EncodePacket(
        TcpPacketType.JoinChannelRequest,
        11,
        new byte[] { 0, 1 });
    var combined = new byte[first.Length + second.Length];

    Array.Copy(first, 0, combined, 0, first.Length);
    Array.Copy(second, 0, combined, first.Length, second.Length);

    var buffer = new TcpReceiveBuffer();
    buffer.Append(combined);

    if (!buffer.TryPop(out TcpPacket? firstPacket) || firstPacket is null)
    {
        throw new InvalidOperationException("The first combined packet was not popped.");
    }

    Assert(firstPacket.Header.RequestId == 10,
        "The first combined packet request ID is incorrect.");

    if (!buffer.TryPop(out TcpPacket? secondPacket) || secondPacket is null)
    {
        throw new InvalidOperationException("The second combined packet was not popped.");
    }

    Assert(secondPacket.Header.RequestId == 11,
        "The second combined packet request ID is incorrect.");
    Assert(!buffer.TryPop(out _), "The receive buffer must now be empty.");
}

static void TestInvalidPacketSize()
{
    var buffer = new TcpReceiveBuffer();
    buffer.Append(new byte[] { 0, 7, 0, 1, 0, 0, 0, 1 });

    try
    {
        buffer.TryPop(out _);
    }
    catch (InvalidDataException)
    {
        return;
    }

    throw new InvalidOperationException("A packet smaller than the header was accepted.");
}

static void TestGamePayloads()
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
            LoginResponseBytes((TcpSchema.LoginResult)4, 0)),
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

static void TestDungeonProtocol()
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

static void TestTcpFlatBufferSchema()
{
    var builder = new FlatBufferBuilder(128);
    StringOffset authTicket = builder.CreateString("valid-ticket");
    Offset<TcpSchema.LoginRequest> login =
        TcpSchema.LoginRequest.CreateLoginRequest(builder, authTicket);
    Offset<TcpSchema.TcpMessage> message =
        TcpSchema.TcpMessage.CreateTcpMessage(
            builder,
            2,
            TcpSchema.TcpPayload.LoginRequest,
            login.Value);
    TcpSchema.TcpMessage.FinishTcpMessageBuffer(builder, message);

    var buffer = new ByteBuffer(builder.SizedByteArray());
    Assert(TcpSchema.TcpMessage.TcpMessageBufferHasIdentifier(buffer),
        "TCP FlatBuffer identifier is incorrect.");
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(buffer),
        "TCP FlatBuffer verification failed.");

    TcpSchema.TcpMessage decoded =
        TcpSchema.TcpMessage.GetRootAsTcpMessage(buffer);
    Assert(decoded.ProtocolVersion == 2 &&
        decoded.PayloadType == TcpSchema.TcpPayload.LoginRequest &&
        decoded.PayloadAsLoginRequest().AuthTicket == "valid-ticket",
        "TCP FlatBuffer login payload is incorrect.");
}

static void TestAuthFlatBufferSchema()
{
    var builder = new FlatBufferBuilder(128);
    StringOffset loginId = builder.CreateString("test-user");
    StringOffset password = builder.CreateString("test-password");
    Offset<AuthSchema.LoginRequest> login =
        AuthSchema.LoginRequest.CreateLoginRequest(
            builder,
            loginId,
            password);
    Offset<AuthSchema.AuthMessage> message =
        AuthSchema.AuthMessage.CreateAuthMessage(
            builder,
            1,
            AuthSchema.AuthPayload.LoginRequest,
            login.Value);
    AuthSchema.AuthMessage.FinishAuthMessageBuffer(builder, message);

    var buffer = new ByteBuffer(builder.SizedByteArray());
    Assert(AuthSchema.AuthMessage.AuthMessageBufferHasIdentifier(buffer),
        "Auth FlatBuffer identifier is incorrect.");
    Assert(AuthSchema.AuthMessage.VerifyAuthMessage(buffer),
        "Auth FlatBuffer verification failed.");

    AuthSchema.AuthMessage decoded =
        AuthSchema.AuthMessage.GetRootAsAuthMessage(buffer);
    AuthSchema.LoginRequest decodedLogin = decoded.PayloadAsLoginRequest();
    Assert(decoded.ProtocolVersion == 1 &&
        decoded.PayloadType == AuthSchema.AuthPayload.LoginRequest &&
        decodedLogin.LoginId == "test-user" &&
        decodedLogin.Password == "test-password",
        "Auth FlatBuffer login payload is incorrect.");
}

static byte[] CreatePartySnapshotResponseBytes(
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

static byte[] DungeonCatalogResponseBytes(
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

static byte[] DungeonStaticDataResponseBytes(
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

static byte[] LoginResponseBytes(
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

static byte[] ChannelListResponseBytes(
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

static byte[] JoinChannelResponseBytes(
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

static byte[] CreatePartyResponseBytes(
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

static byte[] JoinPartyResponseBytes(
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

static byte[] LeavePartyResponseBytes(TcpSchema.LeavePartyResult result)
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

static byte[] EnterDungeonResponseBytes(
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

static byte[] DungeonConnectionInfoResponseBytes(
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

static byte[] CreateTestSnapshotBytes(
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

static byte[] CreateUdpHelloAckBytes(
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

static async Task TestUdpSessionAsync()
{
    using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
    int port = ((IPEndPoint)server.Client.LocalEndPoint!).Port;
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
    using var client = new DungeonUdpService();
    var snapshotCompletion =
        new TaskCompletionSource<DungeonSnapshotData>(
            TaskCreationOptions.RunContinuationsAsynchronously);

    client.SnapshotReceived += snapshot => snapshotCompletion.TrySetResult(snapshot);
    Task<UdpHelloAckData> connectTask = client.ConnectAsync(
        "127.0.0.1",
        port,
        9,
        3,
        77,
        timeout.Token);

    UdpReceiveResult helloResult = await server.ReceiveAsync(timeout.Token);
    var helloBuffer = new ByteBuffer(helloResult.Buffer);
    DungeonMessage helloMessage = DungeonMessage.GetRootAsDungeonMessage(helloBuffer);
    Assert(helloMessage.PayloadType == DungeonPayload.UdpHello,
        "UDP service did not send hello first.");
    Assert(client.IsRunning && !client.IsAuthenticated && !connectTask.IsCompleted,
        "UDP service authenticated before receiving an acknowledgement.");

    byte[] snapshotBytes = CreateTestSnapshotBytes();
    await server.SendAsync(snapshotBytes, helloResult.RemoteEndPoint, timeout.Token);
    await Task.Delay(TimeSpan.FromMilliseconds(30), timeout.Token);
    Assert(!snapshotCompletion.Task.IsCompleted,
        "UDP service published a snapshot before authentication.");

    byte[] ackBytes = CreateUdpHelloAckBytes(
        UdpHelloAckResult.Success,
        44);
    await server.SendAsync(ackBytes, helloResult.RemoteEndPoint, timeout.Token);
    UdpHelloAckData ack = await connectTask;
    Assert(ack.Result == UdpHelloResult.Success && ack.ServerTick == 44 &&
        client.IsAuthenticated,
        "UDP service did not complete successful authentication.");

    await server.SendAsync(snapshotBytes, helloResult.RemoteEndPoint, timeout.Token);
    DungeonSnapshotData receivedSnapshot =
        await snapshotCompletion.Task.WaitAsync(timeout.Token);
    Assert(receivedSnapshot.ServerTick == 45,
        "UDP service did not publish the received snapshot.");

    await client.SendMovementAsync(1.0f, 0.0f, false, timeout.Token);
    UdpReceiveResult movementResult = await server.ReceiveAsync(timeout.Token);
    var movementBuffer = new ByteBuffer(movementResult.Buffer);
    DungeonMessage movementMessage =
        DungeonMessage.GetRootAsDungeonMessage(movementBuffer);
    Assert(movementMessage.PayloadType == DungeonPayload.PlayerMovement &&
        movementMessage.PayloadAsPlayerMovement().Sequence == 1,
        "UDP service movement sequence is incorrect.");

    client.Disconnect();
    Assert(!client.IsRunning && !client.IsAuthenticated,
        "UDP service did not disconnect.");

    using var failureServer = new UdpClient(
        new IPEndPoint(IPAddress.Loopback, 0));
    int failurePort =
        ((IPEndPoint)failureServer.Client.LocalEndPoint!).Port;
    using var failureTimeout =
        new CancellationTokenSource(TimeSpan.FromSeconds(3));
    Task<UdpHelloAckData> failedConnectTask = client.ConnectAsync(
        "127.0.0.1",
        failurePort,
        9,
        3,
        77,
        failureTimeout.Token);
    UdpReceiveResult failedHello =
        await failureServer.ReceiveAsync(failureTimeout.Token);
    byte[] failedAckBytes = CreateUdpHelloAckBytes(
        UdpHelloAckResult.AuthenticationFailed,
        0);
    await failureServer.SendAsync(
        failedAckBytes,
        failedHello.RemoteEndPoint,
        failureTimeout.Token);

    bool authenticationRejected = false;
    try
    {
        await failedConnectTask;
    }
    catch (InvalidDataException)
    {
        authenticationRejected = true;
    }

    Assert(authenticationRejected &&
        !client.IsRunning &&
        !client.IsAuthenticated,
        "UDP authentication failure did not close the client session.");
}

static async Task TestTcpConnectionAsync()
{
    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();

    int port = ((IPEndPoint)listener.LocalEndpoint).Port;
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
    Task<TcpClient> acceptTask =
        listener.AcceptTcpClientAsync(timeout.Token).AsTask();

    using var connection = new TcpConnectionService();
    await connection.ConnectAsync("127.0.0.1", port, timeout.Token);
    using TcpClient acceptedClient = await acceptTask;

    if (!connection.IsConnected)
    {
        throw new InvalidOperationException("TCP connection was not established.");
    }

    Task<TcpPacket> responseTask = connection.SendRequestAsync(
        TcpPacketType.LoginRequest,
        GamePayloadCodec.EncodeLoginRequest("valid-ticket"),
        timeout.Token);

    NetworkStream serverStream = acceptedClient.GetStream();
    var requestHeaderBytes = new byte[TcpPacketCodec.HeaderSize];
    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader requestHeader = TcpPacketCodec.DecodeHeader(requestHeaderBytes);

    int requestPayloadSize = requestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var requestPayload = new byte[requestPayloadSize];
    await serverStream.ReadExactlyAsync(requestPayload, timeout.Token);

    Assert(requestHeader.Type == TcpPacketType.LoginRequest,
        "TCP request type is incorrect.");
    var loginRequestBuffer = new ByteBuffer(requestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(loginRequestBuffer),
        "TCP login request FlatBuffer is invalid.");
    TcpSchema.TcpMessage loginRequestMessage =
        TcpSchema.TcpMessage.GetRootAsTcpMessage(loginRequestBuffer);
    Assert(loginRequestMessage.PayloadType == TcpSchema.TcpPayload.LoginRequest &&
        loginRequestMessage.PayloadAsLoginRequest().AuthTicket == "valid-ticket",
        "TCP login request payload is incorrect.");

    byte[] responseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.LoginResponse,
        requestHeader.RequestId,
        LoginResponseBytes(TcpSchema.LoginResult.Success, 42));

    await serverStream.WriteAsync(responseBytes[..3], timeout.Token);
    await serverStream.WriteAsync(responseBytes[3..], timeout.Token);

    TcpPacket response = await responseTask;
    Assert(response.Header.Type == TcpPacketType.LoginResponse,
        "TCP response type is incorrect.");
    LoginResponseData loginResponse =
        GamePayloadCodec.DecodeLoginResponse(response.Payload);
    Assert(loginResponse.Result == LoginResult.Success &&
        loginResponse.SessionId == 42,
        "TCP login response payload is incorrect.");

    Task<TcpPacket> channelListResponseTask = connection.SendRequestAsync(
        TcpPacketType.ChannelListRequest,
        GamePayloadCodec.EncodeChannelListRequest(),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader channelListRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    int channelListRequestPayloadSize =
        channelListRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var channelListRequestPayload = new byte[channelListRequestPayloadSize];
    await serverStream.ReadExactlyAsync(
        channelListRequestPayload,
        timeout.Token);
    Assert(channelListRequestHeader.Type == TcpPacketType.ChannelListRequest,
        "Channel list TCP request type is incorrect.");
    var channelListRequestBuffer = new ByteBuffer(channelListRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(channelListRequestBuffer),
        "Channel list TCP request FlatBuffer is invalid.");
    Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(channelListRequestBuffer)
        .PayloadType == TcpSchema.TcpPayload.ChannelListRequest,
        "Channel list TCP request FlatBuffer type is incorrect.");

    byte[] channelListResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.ChannelListResponse,
        channelListRequestHeader.RequestId,
        ChannelListResponseBytes(new[]
        {
            (1U, "Channel 1", 2U, 100U)
        }));
    await serverStream.WriteAsync(channelListResponseBytes, timeout.Token);

    TcpPacket channelListResponse = await channelListResponseTask;
    IReadOnlyList<ChannelInfo> receivedChannels =
        GamePayloadCodec.DecodeChannelListResponse(channelListResponse.Payload);
    Assert(receivedChannels.Count == 1 &&
        receivedChannels[0].DisplayName == "Channel 1",
        "Channel list TCP response payload is incorrect.");

    Task<TcpPacket> catalogResponseTask = connection.SendRequestAsync(
        TcpPacketType.DungeonCatalogRequest,
        GamePayloadCodec.EncodeDungeonCatalogRequest(),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader catalogRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    int catalogRequestPayloadSize =
        catalogRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var catalogRequestPayload = new byte[catalogRequestPayloadSize];
    await serverStream.ReadExactlyAsync(catalogRequestPayload, timeout.Token);
    Assert(catalogRequestHeader.Type == TcpPacketType.DungeonCatalogRequest,
        "Dungeon catalog TCP request type is incorrect.");
    var catalogRequestBuffer = new ByteBuffer(catalogRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(catalogRequestBuffer),
        "Dungeon catalog TCP request FlatBuffer is invalid.");
    Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(catalogRequestBuffer)
        .PayloadType == TcpSchema.TcpPayload.DungeonCatalogRequest,
        "Dungeon catalog TCP request FlatBuffer type is incorrect.");

    byte[] catalogResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.DungeonCatalogResponse,
        catalogRequestHeader.RequestId,
        DungeonCatalogResponseBytes(
            TcpSchema.CatalogResult.Success,
            new[]
            {
                (1001U, "Forest", (byte)1, (byte)4, true)
            }));
    await serverStream.WriteAsync(catalogResponseBytes, timeout.Token);

    TcpPacket catalogResponse = await catalogResponseTask;
    DungeonCatalogData receivedCatalog =
        GamePayloadCodec.DecodeDungeonCatalogResponse(catalogResponse.Payload);
    Assert(catalogResponse.Header.Type == TcpPacketType.DungeonCatalogResponse &&
        receivedCatalog.Dungeons.Count == 1 &&
        receivedCatalog.Dungeons[0].TemplateId == 1001,
        "Dungeon catalog TCP response is incorrect.");

    Task<TcpPacket> joinChannelResponseTask = connection.SendRequestAsync(
        TcpPacketType.JoinChannelRequest,
        GamePayloadCodec.EncodeJoinChannelRequest(1),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader joinChannelRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    int joinChannelRequestPayloadSize =
        joinChannelRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var joinChannelRequestPayload = new byte[joinChannelRequestPayloadSize];
    await serverStream.ReadExactlyAsync(
        joinChannelRequestPayload,
        timeout.Token);
    Assert(joinChannelRequestHeader.Type == TcpPacketType.JoinChannelRequest,
        "Join channel TCP request type is incorrect.");
    var joinChannelRequestBuffer = new ByteBuffer(joinChannelRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(joinChannelRequestBuffer),
        "Join channel TCP request FlatBuffer is invalid.");
    TcpSchema.TcpMessage joinChannelRequest =
        TcpSchema.TcpMessage.GetRootAsTcpMessage(joinChannelRequestBuffer);
    Assert(joinChannelRequest.PayloadType ==
        TcpSchema.TcpPayload.JoinChannelRequest &&
        joinChannelRequest.PayloadAsJoinChannelRequest().ChannelId == 1,
        "Join channel TCP request payload is incorrect.");

    byte[] joinChannelResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.JoinChannelResponse,
        joinChannelRequestHeader.RequestId,
        JoinChannelResponseBytes(
            TcpSchema.JoinChannelResult.Success,
            1));
    await serverStream.WriteAsync(joinChannelResponseBytes, timeout.Token);

    TcpPacket joinChannelResponse = await joinChannelResponseTask;
    JoinChannelResponse receivedJoinChannel =
        GamePayloadCodec.DecodeJoinChannelResponse(joinChannelResponse.Payload);
    Assert(receivedJoinChannel.Result == JoinChannelResult.Success &&
        receivedJoinChannel.ChannelId == 1,
        "Join channel TCP response payload is incorrect.");

    Task<TcpPacket> partyResponseTask = connection.SendRequestAsync(
        TcpPacketType.CreatePartyRequest,
        GamePayloadCodec.EncodeCreatePartyRequest(),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader partyRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    Assert(partyRequestHeader.Type == TcpPacketType.CreatePartyRequest,
        "Create party TCP request type is incorrect.");
    int partyRequestPayloadSize =
        partyRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var partyRequestPayload = new byte[partyRequestPayloadSize];
    await serverStream.ReadExactlyAsync(partyRequestPayload, timeout.Token);
    var partyRequestBuffer = new ByteBuffer(partyRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(partyRequestBuffer),
        "Create party TCP request FlatBuffer is invalid.");
    Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(partyRequestBuffer)
        .PayloadType == TcpSchema.TcpPayload.CreatePartyRequest,
        "Create party TCP request FlatBuffer type is incorrect.");

    byte[] partyResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.CreatePartyResponse,
        partyRequestHeader.RequestId,
        CreatePartyResponseBytes(
            TcpSchema.CreatePartyResult.Success,
            9,
            42));
    await serverStream.WriteAsync(partyResponseBytes, timeout.Token);

    TcpPacket partyResponse = await partyResponseTask;
    Assert(partyResponse.Header.Type == TcpPacketType.CreatePartyResponse,
        "Create party TCP response type is incorrect.");

    Task<TcpPacket> joinPartyResponseTask = connection.SendRequestAsync(
        TcpPacketType.JoinPartyRequest,
        GamePayloadCodec.EncodeJoinPartyRequest(9),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader joinPartyRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    int joinPartyRequestPayloadSize =
        joinPartyRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var joinPartyRequestPayload = new byte[joinPartyRequestPayloadSize];
    await serverStream.ReadExactlyAsync(joinPartyRequestPayload, timeout.Token);
    Assert(joinPartyRequestHeader.Type == TcpPacketType.JoinPartyRequest,
        "Join party TCP request type is incorrect.");
    var joinPartyRequestBuffer = new ByteBuffer(joinPartyRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(joinPartyRequestBuffer),
        "Join party TCP request FlatBuffer is invalid.");
    TcpSchema.TcpMessage joinPartyRequest =
        TcpSchema.TcpMessage.GetRootAsTcpMessage(joinPartyRequestBuffer);
    Assert(joinPartyRequest.PayloadType == TcpSchema.TcpPayload.JoinPartyRequest &&
        joinPartyRequest.PayloadAsJoinPartyRequest().PartyId == 9,
        "Join party TCP request FlatBuffer data is incorrect.");

    byte[] joinPartyResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.JoinPartyResponse,
        joinPartyRequestHeader.RequestId,
        JoinPartyResponseBytes(
            TcpSchema.JoinPartyResult.Success,
            9,
            42));
    await serverStream.WriteAsync(joinPartyResponseBytes, timeout.Token);

    TcpPacket joinPartyResponse = await joinPartyResponseTask;
    Assert(joinPartyResponse.Header.Type == TcpPacketType.JoinPartyResponse,
        "Join party TCP response type is incorrect.");

    Task<TcpPacket> snapshotResponseTask = connection.SendRequestAsync(
        TcpPacketType.PartySnapshotRequest,
        GamePayloadCodec.EncodePartySnapshotRequest(),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader snapshotRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    Assert(snapshotRequestHeader.Type == TcpPacketType.PartySnapshotRequest,
        "Party snapshot TCP request type is incorrect.");
    int snapshotRequestPayloadSize =
        snapshotRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var snapshotRequestPayload = new byte[snapshotRequestPayloadSize];
    await serverStream.ReadExactlyAsync(snapshotRequestPayload, timeout.Token);
    var snapshotRequestBuffer = new ByteBuffer(snapshotRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(snapshotRequestBuffer),
        "Party snapshot TCP request FlatBuffer is invalid.");
    Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(snapshotRequestBuffer)
        .PayloadType == TcpSchema.TcpPayload.PartySnapshotRequest,
        "Party snapshot TCP request FlatBuffer type is incorrect.");

    byte[] snapshotResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.PartySnapshotResponse,
        snapshotRequestHeader.RequestId,
        CreatePartySnapshotResponseBytes(
            TcpSchema.PartySnapshotResult.Success,
            9,
            42,
            new ulong[] { 42, 43 }));
    await serverStream.WriteAsync(snapshotResponseBytes, timeout.Token);

    TcpPacket snapshotResponse = await snapshotResponseTask;
    Assert(snapshotResponse.Header.Type == TcpPacketType.PartySnapshotResponse,
        "Party snapshot TCP response type is incorrect.");

    Task<TcpPacket> enterDungeonResponseTask = connection.SendRequestAsync(
        TcpPacketType.EnterDungeonRequest,
        GamePayloadCodec.EncodeEnterDungeonRequest(1001),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader enterDungeonRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    int enterDungeonRequestPayloadSize =
        enterDungeonRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var enterDungeonRequestPayload =
        new byte[enterDungeonRequestPayloadSize];
    await serverStream.ReadExactlyAsync(
        enterDungeonRequestPayload,
        timeout.Token);
    Assert(enterDungeonRequestHeader.Type == TcpPacketType.EnterDungeonRequest,
        "Enter dungeon TCP request type is incorrect.");
    var enterDungeonRequestBuffer =
        new ByteBuffer(enterDungeonRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(enterDungeonRequestBuffer),
        "Enter dungeon TCP request FlatBuffer is invalid.");
    TcpSchema.TcpMessage enterDungeonRequest =
        TcpSchema.TcpMessage.GetRootAsTcpMessage(enterDungeonRequestBuffer);
    Assert(enterDungeonRequest.PayloadType ==
        TcpSchema.TcpPayload.EnterDungeonRequest &&
        enterDungeonRequest.PayloadAsEnterDungeonRequest()
            .DungeonTemplateId == 1001,
        "Enter dungeon TCP request payload is incorrect.");

    byte[] enterDungeonResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.EnterDungeonResponse,
        enterDungeonRequestHeader.RequestId,
        EnterDungeonResponseBytes(
            TcpSchema.EnterDungeonResult.Success,
            10,
            40000,
            77));
    await serverStream.WriteAsync(enterDungeonResponseBytes, timeout.Token);

    TcpPacket enterDungeonResponse = await enterDungeonResponseTask;
    EnterDungeonResponse receivedDungeon =
        GamePayloadCodec.DecodeEnterDungeonResponse(
            enterDungeonResponse.Payload);
    Assert(receivedDungeon.Result == EnterDungeonResult.Success &&
        receivedDungeon.DungeonId == 10 &&
        receivedDungeon.UdpPort == 40000 &&
        receivedDungeon.UdpToken == 77,
        "Enter dungeon TCP response payload is incorrect.");

    Task<TcpPacket> staticDataResponseTask = connection.SendRequestAsync(
        TcpPacketType.DungeonStaticDataRequest,
        DungeonStaticDataCodec.EncodeRequest(receivedDungeon.DungeonId),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader staticDataRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    int staticDataRequestPayloadSize =
        staticDataRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var staticDataRequestPayload =
        new byte[staticDataRequestPayloadSize];
    await serverStream.ReadExactlyAsync(
        staticDataRequestPayload,
        timeout.Token);
    Assert(staticDataRequestHeader.Type ==
        TcpPacketType.DungeonStaticDataRequest,
        "Dungeon static data TCP request type is incorrect.");
    var staticDataRequestBuffer = new ByteBuffer(staticDataRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(staticDataRequestBuffer),
        "Dungeon static data TCP request FlatBuffer is invalid.");
    TcpSchema.TcpMessage staticDataRequest =
        TcpSchema.TcpMessage.GetRootAsTcpMessage(staticDataRequestBuffer);
    Assert(staticDataRequest.PayloadType ==
        TcpSchema.TcpPayload.DungeonStaticDataRequest &&
        staticDataRequest.PayloadAsDungeonStaticDataRequest().DungeonId == 10,
        "Dungeon static data TCP request payload is incorrect.");

    byte[] staticDataResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.DungeonStaticDataResponse,
        staticDataRequestHeader.RequestId,
        DungeonStaticDataResponseBytes(
            TcpSchema.DungeonStaticDataResult.Success,
            10,
            1001));
    await serverStream.WriteAsync(staticDataResponseBytes, timeout.Token);

    TcpPacket staticDataResponse = await staticDataResponseTask;
    DungeonStaticData receivedStaticData =
        DungeonStaticDataCodec.DecodeResponse(staticDataResponse.Payload);
    Assert(staticDataResponse.Header.Type ==
        TcpPacketType.DungeonStaticDataResponse &&
        receivedStaticData.Rooms.Count == 2 &&
        receivedStaticData.EnemyTemplates.Count == 1,
        "Dungeon static data TCP response is incorrect.");

    Task<TcpPacket> connectionInfoResponseTask = connection.SendRequestAsync(
        TcpPacketType.DungeonConnectionInfoRequest,
        GamePayloadCodec.EncodeDungeonConnectionInfoRequest(),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader connectionInfoRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    int connectionInfoRequestPayloadSize =
        connectionInfoRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var connectionInfoRequestPayload =
        new byte[connectionInfoRequestPayloadSize];
    await serverStream.ReadExactlyAsync(
        connectionInfoRequestPayload,
        timeout.Token);
    Assert(connectionInfoRequestHeader.Type ==
        TcpPacketType.DungeonConnectionInfoRequest,
        "Dungeon connection info TCP request type is incorrect.");
    var connectionInfoRequestBuffer =
        new ByteBuffer(connectionInfoRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(connectionInfoRequestBuffer),
        "Dungeon connection info TCP request FlatBuffer is invalid.");
    Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(connectionInfoRequestBuffer)
        .PayloadType == TcpSchema.TcpPayload.DungeonConnectionInfoRequest,
        "Dungeon connection info TCP request FlatBuffer type is incorrect.");

    byte[] connectionInfoResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.DungeonConnectionInfoResponse,
        connectionInfoRequestHeader.RequestId,
        DungeonConnectionInfoResponseBytes(
            TcpSchema.DungeonConnectionInfoResult.Success,
            10,
            40000,
            88));
    await serverStream.WriteAsync(connectionInfoResponseBytes, timeout.Token);

    TcpPacket connectionInfoResponse = await connectionInfoResponseTask;
    DungeonConnectionInfo receivedConnectionInfo =
        GamePayloadCodec.DecodeDungeonConnectionInfoResponse(
            connectionInfoResponse.Payload);
    Assert(receivedConnectionInfo.Result ==
        DungeonConnectionInfoResult.Success &&
        receivedConnectionInfo.DungeonId == 10 &&
        receivedConnectionInfo.UdpPort == 40000 &&
        receivedConnectionInfo.UdpToken == 88,
        "Dungeon connection info TCP response payload is incorrect.");

    Task<TcpPacket> leavePartyResponseTask = connection.SendRequestAsync(
        TcpPacketType.LeavePartyRequest,
        GamePayloadCodec.EncodeLeavePartyRequest(),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader leavePartyRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    Assert(leavePartyRequestHeader.Type == TcpPacketType.LeavePartyRequest,
        "Leave party TCP request type is incorrect.");
    int leavePartyRequestPayloadSize =
        leavePartyRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
    var leavePartyRequestPayload = new byte[leavePartyRequestPayloadSize];
    await serverStream.ReadExactlyAsync(leavePartyRequestPayload, timeout.Token);
    var leavePartyRequestBuffer = new ByteBuffer(leavePartyRequestPayload);
    Assert(TcpSchema.TcpMessage.VerifyTcpMessage(leavePartyRequestBuffer),
        "Leave party TCP request FlatBuffer is invalid.");
    Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(leavePartyRequestBuffer)
        .PayloadType == TcpSchema.TcpPayload.LeavePartyRequest,
        "Leave party TCP request FlatBuffer type is incorrect.");

    byte[] leavePartyResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.LeavePartyResponse,
        leavePartyRequestHeader.RequestId,
        LeavePartyResponseBytes(TcpSchema.LeavePartyResult.Success));
    await serverStream.WriteAsync(leavePartyResponseBytes, timeout.Token);

    TcpPacket leavePartyResponse = await leavePartyResponseTask;
    Assert(leavePartyResponse.Header.Type == TcpPacketType.LeavePartyResponse,
        "Leave party TCP response type is incorrect.");

    connection.Disconnect();

    if (connection.IsConnected)
    {
        throw new InvalidOperationException("TCP connection was not closed.");
    }
}

static async Task TestAuthTlsConnectionAsync()
{
    using RSA key = RSA.Create(2048);
    var certificateRequest = new CertificateRequest(
        "CN=localhost",
        key,
        HashAlgorithmName.SHA256,
        RSASignaturePadding.Pkcs1);
    certificateRequest.CertificateExtensions.Add(
        new X509BasicConstraintsExtension(false, false, 0, false));
    certificateRequest.CertificateExtensions.Add(
        new X509KeyUsageExtension(
            X509KeyUsageFlags.DigitalSignature |
            X509KeyUsageFlags.KeyEncipherment,
            false));
    certificateRequest.CertificateExtensions.Add(
        new X509SubjectKeyIdentifierExtension(
            certificateRequest.PublicKey,
            false));
    using X509Certificate2 certificate =
        certificateRequest.CreateSelfSigned(
            DateTimeOffset.UtcNow.AddMinutes(-1),
            DateTimeOffset.UtcNow.AddMinutes(5));

    using var listener = new TcpListener(IPAddress.Loopback, 0);
    listener.Start();
    int port = ((IPEndPoint)listener.LocalEndpoint).Port;
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
    Task<TcpClient> acceptTask =
        listener.AcceptTcpClientAsync(timeout.Token).AsTask();

    var connection = new AuthTlsConnectionService();
    using var authenticationClient = new AuthenticationClient(connection);
    Task connectTask = authenticationClient.ConnectAsync(
        "localhost",
        port,
        timeout.Token,
        (_, serverCertificate, _, _) =>
            serverCertificate?.GetCertHashString(HashAlgorithmName.SHA256) ==
            certificate.GetCertHashString(HashAlgorithmName.SHA256));

    using TcpClient acceptedClient = await acceptTask;
    using var serverStream = new SslStream(
        acceptedClient.GetStream(),
        leaveInnerStreamOpen: false);
    await serverStream.AuthenticateAsServerAsync(
        certificate,
        clientCertificateRequired: false,
        SslProtocols.Tls12 | SslProtocols.Tls13,
        checkCertificateRevocation: false);
    await connectTask;

    Assert(authenticationClient.IsConnected,
        "Authentication TLS connection was not established.");

    bool gamePacketRejected = false;
    try
    {
        await connection.SendRequestAsync(
            TcpPacketType.LoginRequest,
            Array.Empty<byte>(),
            timeout.Token);
    }
    catch (ArgumentException)
    {
        gamePacketRejected = true;
    }
    Assert(gamePacketRejected,
        "Authentication TLS service accepted a game server packet type.");

    byte[] requestPayload = { 1, 2, 3 };
    Task<TcpPacket> responseTask = connection.SendRequestAsync(
        TcpPacketType.AuthLoginRequest,
        requestPayload,
        timeout.Token);

    var headerBytes = new byte[TcpPacketCodec.HeaderSize];
    await serverStream.ReadExactlyAsync(headerBytes, timeout.Token);
    TcpPacketHeader requestHeader = TcpPacketCodec.DecodeHeader(headerBytes);
    var receivedPayload = new byte[
        requestHeader.PacketSize - TcpPacketCodec.HeaderSize];
    await serverStream.ReadExactlyAsync(receivedPayload, timeout.Token);

    Assert(requestHeader.Type == TcpPacketType.AuthLoginRequest,
        "Authentication TLS request type is incorrect.");
    Assert(receivedPayload.SequenceEqual(requestPayload),
        "Authentication TLS request payload is incorrect.");

    byte[] responseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.AuthLoginResponse,
        requestHeader.RequestId,
        new byte[] { 4, 5, 6 });
    await serverStream.WriteAsync(responseBytes, timeout.Token);

    TcpPacket response = await responseTask;
    Assert(response.Header.Type == TcpPacketType.AuthLoginResponse,
        "Authentication TLS response type is incorrect.");
    Assert(response.Payload.SequenceEqual(new byte[] { 4, 5, 6 }),
        "Authentication TLS response payload is incorrect.");

    Task<AuthLoginResult> loginTask = authenticationClient.LoginAsync(
        "account_1",
        "password",
        timeout.Token);

    await serverStream.ReadExactlyAsync(headerBytes, timeout.Token);
    TcpPacketHeader loginHeader = TcpPacketCodec.DecodeHeader(headerBytes);
    var loginPayload = new byte[
        loginHeader.PacketSize - TcpPacketCodec.HeaderSize];
    await serverStream.ReadExactlyAsync(loginPayload, timeout.Token);

    var loginBuffer = new ByteBuffer(loginPayload);
    Assert(AuthSchema.AuthMessage.VerifyAuthMessage(loginBuffer),
        "Authentication client sent an invalid FlatBuffer.");
    AuthSchema.AuthMessage loginMessage =
        AuthSchema.AuthMessage.GetRootAsAuthMessage(loginBuffer);
    AuthSchema.LoginRequest loginRequest =
        loginMessage.PayloadAsLoginRequest();
    Assert(loginHeader.Type == TcpPacketType.AuthLoginRequest &&
        loginMessage.PayloadType == AuthSchema.AuthPayload.LoginRequest &&
        loginRequest.LoginId == "account_1" &&
        loginRequest.Password == "password",
        "Authentication client login request is incorrect.");

    var loginResponseBuilder = new FlatBufferBuilder(64);
    Offset<AuthSchema.LoginResponse> loginResponse =
        AuthSchema.LoginResponse.CreateLoginResponse(
            loginResponseBuilder,
            AuthSchema.LoginResult.Success);
    Offset<AuthSchema.AuthMessage> loginResponseMessage =
        AuthSchema.AuthMessage.CreateAuthMessage(
            loginResponseBuilder,
            1,
            AuthSchema.AuthPayload.LoginResponse,
            loginResponse.Value);
    AuthSchema.AuthMessage.FinishAuthMessageBuffer(
        loginResponseBuilder,
        loginResponseMessage);
    byte[] loginResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.AuthLoginResponse,
        loginHeader.RequestId,
        loginResponseBuilder.SizedByteArray());
    await serverStream.WriteAsync(loginResponseBytes, timeout.Token);

    Assert(await loginTask == AuthLoginResult.Success,
        "Authentication client did not decode the login response.");
}

static void Assert(bool condition, string message)
{
    if (!condition)
    {
        throw new InvalidOperationException(message);
    }
}

static void AssertThrows<TException>(Action action, string message)
    where TException : Exception
{
    try
    {
        action();
    }
    catch (TException)
    {
        return;
    }

    throw new InvalidOperationException(message);
}
