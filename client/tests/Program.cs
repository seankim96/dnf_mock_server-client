using System.IO;
using System.Net;
using System.Net.Sockets;
using Dnf.Protocol;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;
using Google.FlatBuffers;

TestHeaderEncoding();
TestSplitPacket();
TestCombinedPackets();
TestInvalidPacketSize();
TestGamePayloads();
TestDungeonProtocol();
await TestTcpConnectionAsync();
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
    byte[] loginPayload = GamePayloadCodec.EncodeLoginRequest("Player_1");
    Assert(loginPayload.SequenceEqual("Player_1"u8.ToArray()),
        "Login request payload is incorrect.");
    LoginResponseData login = GamePayloadCodec.DecodeLoginResponse(
        new byte[] { 0, 0, 0, 0, 0, 0, 0, 0, 42 });
    Assert(login.Result == LoginResult.Success && login.SessionId == 42,
        "Login response payload is incorrect.");

    LoginResponseData failedLogin = GamePayloadCodec.DecodeLoginResponse(
        new byte[] { 1, 0, 0, 0, 0, 0, 0, 0, 0 });
    Assert(failedLogin.Result == LoginResult.EmptyPlayerName &&
        failedLogin.SessionId == 0, "Failed login payload is incorrect.");

    AssertThrows<InvalidDataException>(
        () => GamePayloadCodec.DecodeLoginResponse(
            new byte[] { 0, 0, 0, 0, 0, 0, 0, 0, 0 }),
        "Successful login with a zero session ID was accepted.");
    AssertThrows<InvalidDataException>(
        () => GamePayloadCodec.DecodeLoginResponse(
            new byte[] { 1, 0, 0, 0, 0, 0, 0, 0, 42 }),
        "Failed login with a non-zero session ID was accepted.");

    byte[] channelListPayload =
    {
        0, 1,
        0, 0, 0, 2,
        0, 0, 0, 5,
        0, 0, 0, 100
    };
    IReadOnlyList<ChannelInfo> channels =
        GamePayloadCodec.DecodeChannelListResponse(channelListPayload);
    Assert(channels.Count == 1, "Channel count is incorrect.");
    Assert(channels[0].Id == 2 && channels[0].CurrentPlayers == 5 &&
        channels[0].MaxPlayers == 100, "Channel entry is incorrect.");

    byte[] joinPayload = GamePayloadCodec.EncodeJoinChannelRequest(3);
    Assert(joinPayload.SequenceEqual(new byte[] { 0, 0, 0, 3 }),
        "Join channel request payload is incorrect.");
    JoinChannelResponse joinResponse = GamePayloadCodec.DecodeJoinChannelResponse(
        new byte[] { 0, 0, 0, 0, 3 });
    Assert(joinResponse.Result == JoinChannelResult.Success &&
        joinResponse.ChannelId == 3, "Join channel response is incorrect.");

    Assert(GamePayloadCodec.EncodeCreatePartyRequest().Length == 0,
        "Create party request payload must be empty.");
    CreatePartyResponse createParty = GamePayloadCodec.DecodeCreatePartyResponse(
        new byte[]
        {
            0,
            0, 0, 0, 0, 0, 0, 0, 9,
            0, 0, 0, 0, 0, 0, 0, 42
        });
    Assert(createParty.Result == CreatePartyResult.Success &&
        createParty.PartyId == 9 && createParty.LeaderSessionId == 42,
        "Create party response is incorrect.");
    CreatePartyResponse failedParty = GamePayloadCodec.DecodeCreatePartyResponse(
        new byte[17] { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    Assert(failedParty.Result == CreatePartyResult.AlreadyInParty &&
        failedParty.PartyId == 0 && failedParty.LeaderSessionId == 0,
        "Failed create party response is incorrect.");
    AssertThrows<InvalidDataException>(
        () => GamePayloadCodec.DecodeCreatePartyResponse(
            new byte[17] { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }),
        "Successful create party response with zero IDs was accepted.");

    byte[] joinPartyPayload = GamePayloadCodec.EncodeJoinPartyRequest(9);
    Assert(joinPartyPayload.SequenceEqual(
        new byte[] { 0, 0, 0, 0, 0, 0, 0, 9 }),
        "Join party request payload is incorrect.");
    JoinPartyResponse joinParty = GamePayloadCodec.DecodeJoinPartyResponse(
        new byte[]
        {
            0,
            0, 0, 0, 0, 0, 0, 0, 9,
            0, 0, 0, 0, 0, 0, 0, 42
        });
    Assert(joinParty.Result == JoinPartyResult.Success &&
        joinParty.PartyId == 9 && joinParty.LeaderSessionId == 42,
        "Join party response is incorrect.");
    JoinPartyResponse missingParty = GamePayloadCodec.DecodeJoinPartyResponse(
        new byte[17] { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    Assert(missingParty.Result == JoinPartyResult.PartyNotFound &&
        missingParty.PartyId == 0 && missingParty.LeaderSessionId == 0,
        "Failed join party response is incorrect.");

    Assert(GamePayloadCodec.EncodeLeavePartyRequest().Length == 0,
        "Leave party request payload must be empty.");
    Assert(GamePayloadCodec.DecodeLeavePartyResponse(new byte[] { 0 }) ==
        LeavePartyResult.Success, "Leave party success result is incorrect.");
    Assert(GamePayloadCodec.DecodeLeavePartyResponse(new byte[] { 1 }) ==
        LeavePartyResult.NotInParty, "Leave party failure result is incorrect.");
    AssertThrows<InvalidDataException>(
        () => GamePayloadCodec.DecodeLeavePartyResponse(new byte[] { 2 }),
        "Unknown leave party result was accepted.");

    Assert(GamePayloadCodec.EncodePartySnapshotRequest().Length == 0,
        "Party snapshot request payload must be empty.");
    PartySnapshotData partySnapshot =
        GamePayloadCodec.DecodePartySnapshotResponse(
            new byte[]
            {
                0,
                0, 0, 0, 0, 0, 0, 0, 9,
                0, 0, 0, 0, 0, 0, 0, 42,
                2,
                0, 0, 0, 0, 0, 0, 0, 42,
                0, 0, 0, 0, 0, 0, 0, 43
            });
    Assert(partySnapshot.Result == PartySnapshotResult.Success &&
        partySnapshot.PartyId == 9 && partySnapshot.LeaderSessionId == 42 &&
        partySnapshot.Members.SequenceEqual(new ulong[] { 42, 43 }),
        "Party snapshot response is incorrect.");
    PartySnapshotData noPartySnapshot =
        GamePayloadCodec.DecodePartySnapshotResponse(
            new byte[18] { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 });
    Assert(noPartySnapshot.Result == PartySnapshotResult.NotInParty &&
        noPartySnapshot.Members.Count == 0,
        "Not-in-party snapshot response is incorrect.");
    AssertThrows<InvalidDataException>(
        () => GamePayloadCodec.DecodePartySnapshotResponse(
            new byte[]
            {
                0,
                0, 0, 0, 0, 0, 0, 0, 9,
                0, 0, 0, 0, 0, 0, 0, 42,
                1,
                0, 0, 0, 0, 0, 0, 0, 43
            }),
        "Party snapshot without the leader was accepted.");

    byte[] dungeonPayload =
    {
        0,
        0, 0, 0, 0, 0, 0, 0, 9,
        0x23, 0x45,
        0, 0, 0, 0, 0, 0, 0, 7
    };
    EnterDungeonResponse dungeonResponse =
        GamePayloadCodec.DecodeEnterDungeonResponse(dungeonPayload);
    Assert(dungeonResponse.Result == EnterDungeonResult.Success &&
        dungeonResponse.DungeonId == 9 && dungeonResponse.UdpPort == 0x2345 &&
        dungeonResponse.UdpToken == 7, "Dungeon response is incorrect.");
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
        decoded.Players[0].X == 100.0f && decoded.Players[0].Y == 250.0f,
        "Dungeon snapshot values are incorrect.");
}

static byte[] CreateTestSnapshotBytes()
{
    var builder = new FlatBufferBuilder(256);
    PlayerSnapshot.StartPlayerSnapshot(builder);
    PlayerSnapshot.AddSessionId(builder, 3);
    PlayerSnapshot.AddRoomId(builder, 1);
    Offset<Vec3> position = Vec3.CreateVec3(builder, 100.0f, 250.0f, 0.0f);
    PlayerSnapshot.AddPosition(builder, position);
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
        enemies);
    Offset<DungeonMessage> snapshotMessage = DungeonMessage.CreateDungeonMessage(
        builder,
        1,
        9,
        DungeonPayload.DungeonSnapshot,
        snapshot.Value);
    DungeonMessage.FinishDungeonMessageBuffer(builder, snapshotMessage);
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
    await client.ConnectAsync("127.0.0.1", port, 9, 3, 77, timeout.Token);

    UdpReceiveResult helloResult = await server.ReceiveAsync(timeout.Token);
    var helloBuffer = new ByteBuffer(helloResult.Buffer);
    DungeonMessage helloMessage = DungeonMessage.GetRootAsDungeonMessage(helloBuffer);
    Assert(helloMessage.PayloadType == DungeonPayload.UdpHello,
        "UDP service did not send hello first.");

    byte[] snapshotBytes = CreateTestSnapshotBytes();
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
    Assert(!client.IsRunning, "UDP service did not disconnect.");
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
        new byte[] { (byte)'P', (byte)'1' },
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
    Assert(requestPayload.SequenceEqual(new byte[] { (byte)'P', (byte)'1' }),
        "TCP request payload is incorrect.");

    byte[] responseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.LoginResponse,
        requestHeader.RequestId,
        new byte[] { 0, 0, 0, 0, 0, 0, 0, 0, 42 });

    await serverStream.WriteAsync(responseBytes[..3], timeout.Token);
    await serverStream.WriteAsync(responseBytes[3..], timeout.Token);

    TcpPacket response = await responseTask;
    Assert(response.Header.Type == TcpPacketType.LoginResponse,
        "TCP response type is incorrect.");
    Assert(response.Payload.SequenceEqual(
        new byte[] { 0, 0, 0, 0, 0, 0, 0, 0, 42 }),
        "TCP response payload is incorrect.");

    Task<TcpPacket> partyResponseTask = connection.SendRequestAsync(
        TcpPacketType.CreatePartyRequest,
        GamePayloadCodec.EncodeCreatePartyRequest(),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader partyRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    Assert(partyRequestHeader.Type == TcpPacketType.CreatePartyRequest,
        "Create party TCP request type is incorrect.");
    Assert(partyRequestHeader.PacketSize == TcpPacketCodec.HeaderSize,
        "Create party TCP request payload must be empty.");

    byte[] partyResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.CreatePartyResponse,
        partyRequestHeader.RequestId,
        new byte[]
        {
            0,
            0, 0, 0, 0, 0, 0, 0, 9,
            0, 0, 0, 0, 0, 0, 0, 42
        });
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
    var joinPartyRequestPayload = new byte[8];
    await serverStream.ReadExactlyAsync(joinPartyRequestPayload, timeout.Token);
    Assert(joinPartyRequestHeader.Type == TcpPacketType.JoinPartyRequest,
        "Join party TCP request type is incorrect.");
    Assert(joinPartyRequestPayload.SequenceEqual(
        new byte[] { 0, 0, 0, 0, 0, 0, 0, 9 }),
        "Join party TCP request payload is incorrect.");

    byte[] joinPartyResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.JoinPartyResponse,
        joinPartyRequestHeader.RequestId,
        new byte[]
        {
            0,
            0, 0, 0, 0, 0, 0, 0, 9,
            0, 0, 0, 0, 0, 0, 0, 42
        });
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
    Assert(snapshotRequestHeader.PacketSize == TcpPacketCodec.HeaderSize,
        "Party snapshot TCP request payload must be empty.");

    byte[] snapshotResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.PartySnapshotResponse,
        snapshotRequestHeader.RequestId,
        new byte[]
        {
            0,
            0, 0, 0, 0, 0, 0, 0, 9,
            0, 0, 0, 0, 0, 0, 0, 42,
            2,
            0, 0, 0, 0, 0, 0, 0, 42,
            0, 0, 0, 0, 0, 0, 0, 43
        });
    await serverStream.WriteAsync(snapshotResponseBytes, timeout.Token);

    TcpPacket snapshotResponse = await snapshotResponseTask;
    Assert(snapshotResponse.Header.Type == TcpPacketType.PartySnapshotResponse,
        "Party snapshot TCP response type is incorrect.");

    Task<TcpPacket> leavePartyResponseTask = connection.SendRequestAsync(
        TcpPacketType.LeavePartyRequest,
        GamePayloadCodec.EncodeLeavePartyRequest(),
        timeout.Token);

    await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
    TcpPacketHeader leavePartyRequestHeader =
        TcpPacketCodec.DecodeHeader(requestHeaderBytes);
    Assert(leavePartyRequestHeader.Type == TcpPacketType.LeavePartyRequest,
        "Leave party TCP request type is incorrect.");
    Assert(leavePartyRequestHeader.PacketSize == TcpPacketCodec.HeaderSize,
        "Leave party TCP request payload must be empty.");

    byte[] leavePartyResponseBytes = TcpPacketCodec.EncodePacket(
        TcpPacketType.LeavePartyResponse,
        leavePartyRequestHeader.RequestId,
        new byte[] { 0 });
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
