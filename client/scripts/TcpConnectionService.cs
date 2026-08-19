using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using DnfMockClient.Protocol;

namespace DnfMockClient.Networking;

public sealed class TcpConnectionService : IDisposable
{
    private TcpClient? _client;
    private readonly SemaphoreSlim _requestLock = new(1, 1);
    private readonly TcpReceiveBuffer _receiveBuffer = new();
    private uint _nextRequestId = 1;

    public bool IsConnected => _client?.Connected == true;

    public async Task ConnectAsync(
        string host,
        int port,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(host))
        {
            throw new ArgumentException("Server address is required.", nameof(host));
        }

        if (port is < 1 or > 65535)
        {
            throw new ArgumentOutOfRangeException(nameof(port));
        }

        Disconnect();

        var client = new TcpClient();

        try
        {
            await client.ConnectAsync(host, port, cancellationToken);
            _client = client;
            _receiveBuffer.Clear();
            _nextRequestId = 1;
        }
        catch
        {
            client.Dispose();
            throw;
        }
    }

    public async Task<TcpPacket> SendRequestAsync(
        TcpPacketType requestType,
        byte[] payload,
        CancellationToken cancellationToken)
    {
        await _requestLock.WaitAsync(cancellationToken);

        try
        {
            TcpClient client = _client ?? throw new InvalidOperationException(
                "TCP client is not connected.");

            uint requestId = _nextRequestId++;
            byte[] requestBytes = TcpPacketCodec.EncodePacket(
                requestType,
                requestId,
                payload);

            NetworkStream stream = client.GetStream();
            await stream.WriteAsync(requestBytes, cancellationToken);

            TcpPacket response = await ReadPacketAsync(stream, cancellationToken);
            TcpPacketType expectedType = GetResponseType(requestType);

            if (response.Header.RequestId != requestId)
            {
                throw new InvalidDataException("TCP response request ID does not match.");
            }

            if (response.Header.Type != expectedType)
            {
                throw new InvalidDataException("Unexpected TCP response type.");
            }

            return response;
        }
        finally
        {
            _requestLock.Release();
        }
    }

    public void Disconnect()
    {
        _client?.Dispose();
        _client = null;
        _receiveBuffer.Clear();
    }

    public void Dispose()
    {
        Disconnect();
    }

    private async Task<TcpPacket> ReadPacketAsync(
        NetworkStream stream,
        CancellationToken cancellationToken)
    {
        var receivedBytes = new byte[4096];

        while (true)
        {
            if (_receiveBuffer.TryPop(out TcpPacket? packet) && packet is not null)
            {
                return packet;
            }

            int receivedSize = await stream.ReadAsync(
                receivedBytes,
                cancellationToken);

            if (receivedSize == 0)
            {
                throw new EndOfStreamException("TCP server closed the connection.");
            }

            _receiveBuffer.Append(receivedBytes[..receivedSize]);
        }
    }

    private static TcpPacketType GetResponseType(TcpPacketType requestType)
    {
        return requestType switch
        {
            TcpPacketType.LoginRequest => TcpPacketType.LoginResponse,
            TcpPacketType.ChannelListRequest => TcpPacketType.ChannelListResponse,
            TcpPacketType.JoinChannelRequest => TcpPacketType.JoinChannelResponse,
            TcpPacketType.EnterDungeonRequest => TcpPacketType.EnterDungeonResponse,
            TcpPacketType.DungeonConnectionInfoRequest =>
                TcpPacketType.DungeonConnectionInfoResponse,
            _ => throw new ArgumentException("Not a TCP request type.", nameof(requestType))
        };
    }
}
