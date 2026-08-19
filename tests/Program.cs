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
    Assert(GamePayloadCodec.DecodeLoginResponse(new byte[] { 0 }) == LoginResult.Success,
        "Login response payload is incorrect.");

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

    Assert(DungeonProtocolCodec.TryDecodeSnapshot(
        builder.SizedByteArray(),
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
        new byte[] { 0 });

    await serverStream.WriteAsync(responseBytes[..3], timeout.Token);
    await serverStream.WriteAsync(responseBytes[3..], timeout.Token);

    TcpPacket response = await responseTask;
    Assert(response.Header.Type == TcpPacketType.LoginResponse,
        "TCP response type is incorrect.");
    Assert(response.Payload.SequenceEqual(new byte[] { 0 }),
        "TCP response payload is incorrect.");

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
