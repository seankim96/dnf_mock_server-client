using System.IO;
using System.Net;
using System.Net.Sockets;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;

TestHeaderEncoding();
TestSplitPacket();
TestCombinedPackets();
TestInvalidPacketSize();
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
