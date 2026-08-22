using System;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using DnfMockClient.Protocol;

namespace DnfMockClient.Networking;

public sealed class DungeonUdpService : IDisposable
{
    private const int MaxDatagramSize = 1200;

    private readonly SemaphoreSlim _sendLock = new(1, 1);
    private UdpClient? _client;
    private CancellationTokenSource? _sessionCancellation;
    private Task? _receiveTask;
    private Task? _heartbeatTask;
    private TaskCompletionSource<UdpHelloAckData>? _helloAckCompletion;
    private volatile bool _authenticated;
    private ulong _dungeonId;
    private ulong _sessionId;
    private uint _movementSequence;
    private uint _attackSequence;

    public event Action<DungeonSnapshotData>? SnapshotReceived;
    public event Action<Exception>? ErrorOccurred;

    public bool IsRunning => _client is not null;
    public bool IsAuthenticated => _authenticated;

    public async Task<UdpHelloAckData> ConnectAsync(
        string host,
        int port,
        ulong dungeonId,
        ulong sessionId,
        ulong token,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(host))
        {
            throw new ArgumentException("UDP server address is required.", nameof(host));
        }

        if (port is < 1 or > 65535)
        {
            throw new ArgumentOutOfRangeException(nameof(port));
        }

        Disconnect();

        var client = new UdpClient(AddressFamily.InterNetwork);
        try
        {
            client.Connect(host, port);
            _client = client;
            _dungeonId = dungeonId;
            _sessionId = sessionId;
            _movementSequence = 0;
            _attackSequence = 0;
            _sessionCancellation = new CancellationTokenSource();
            _helloAckCompletion = new TaskCompletionSource<UdpHelloAckData>(
                TaskCreationOptions.RunContinuationsAsynchronously);
            _receiveTask = ReceiveLoopAsync(
                client,
                _sessionCancellation.Token);

            byte[] hello = DungeonProtocolCodec.EncodeUdpHello(
                dungeonId,
                sessionId,
                token);
            await SendBytesAsync(client, hello, cancellationToken);

            UdpHelloAckData ack = await _helloAckCompletion.Task.WaitAsync(
                cancellationToken);
            if (ack.Result != UdpHelloResult.Success)
            {
                throw new InvalidDataException(
                    $"UDP authentication failed: {ack.Result}.");
            }

            _helloAckCompletion = null;
            _heartbeatTask = HeartbeatLoopAsync(
                client,
                _sessionCancellation.Token);
            return ack;
        }
        catch
        {
            Disconnect();
            throw;
        }
    }

    public async Task SendMovementAsync(
        float moveX,
        float moveY,
        bool jump,
        CancellationToken cancellationToken = default)
    {
        UdpClient client = GetAuthenticatedClient();
        uint sequence = NextSequence(ref _movementSequence);
        byte[] bytes = DungeonProtocolCodec.EncodePlayerMovement(
            _dungeonId,
            sequence,
            moveX,
            moveY,
            jump);
        await SendBytesAsync(client, bytes, cancellationToken);
    }

    public async Task SendAttackAsync(
        uint skillId,
        float directionX,
        float directionY,
        CancellationToken cancellationToken = default)
    {
        UdpClient client = GetAuthenticatedClient();
        uint sequence = NextSequence(ref _attackSequence);
        byte[] bytes = DungeonProtocolCodec.EncodePlayerAttack(
            _dungeonId,
            sequence,
            skillId,
            directionX,
            directionY);
        await SendBytesAsync(client, bytes, cancellationToken);
    }

    public void Disconnect()
    {
        _authenticated = false;
        _helloAckCompletion?.TrySetCanceled();
        _sessionCancellation?.Cancel();
        _client?.Dispose();
        _sessionCancellation?.Dispose();

        _client = null;
        _sessionCancellation = null;
        _receiveTask = null;
        _heartbeatTask = null;
        _helloAckCompletion = null;
        _dungeonId = 0;
        _sessionId = 0;
    }

    public void Dispose()
    {
        Disconnect();
    }

    private async Task ReceiveLoopAsync(
        UdpClient client,
        CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                UdpReceiveResult received = await client.ReceiveAsync(cancellationToken);
                if (received.Buffer.Length > MaxDatagramSize)
                {
                    continue;
                }

                if (DungeonProtocolCodec.TryDecodeUdpHelloAck(
                        received.Buffer,
                        out UdpHelloAckData? ack) &&
                    ack is not null &&
                    ack.DungeonId == _dungeonId)
                {
                    if (ack.Result == UdpHelloResult.Success)
                    {
                        _authenticated = true;
                    }

                    _helloAckCompletion?.TrySetResult(ack);
                    continue;
                }

                if (_authenticated &&
                    DungeonProtocolCodec.TryDecodeSnapshot(
                        received.Buffer,
                        out DungeonSnapshotData? snapshot) &&
                    snapshot is not null &&
                    snapshot.DungeonId == _dungeonId)
                {
                    SnapshotReceived?.Invoke(snapshot);
                }
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (SocketException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            if (!(_helloAckCompletion?.TrySetException(exception) ?? false))
            {
                ErrorOccurred?.Invoke(exception);
            }
        }
    }

    private async Task HeartbeatLoopAsync(
        UdpClient client,
        CancellationToken cancellationToken)
    {
        try
        {
            while (!cancellationToken.IsCancellationRequested)
            {
                await Task.Delay(TimeSpan.FromSeconds(1), cancellationToken);
                byte[] heartbeat = DungeonProtocolCodec.EncodeUdpHeartbeat(
                    _dungeonId,
                    _sessionId);
                await SendBytesAsync(client, heartbeat, cancellationToken);
            }
        }
        catch (OperationCanceledException)
        {
        }
        catch (ObjectDisposedException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (SocketException) when (cancellationToken.IsCancellationRequested)
        {
        }
        catch (Exception exception)
        {
            ErrorOccurred?.Invoke(exception);
        }
    }

    private async Task SendBytesAsync(
        UdpClient client,
        byte[] bytes,
        CancellationToken cancellationToken)
    {
        await _sendLock.WaitAsync(cancellationToken);

        try
        {
            await client.SendAsync(bytes.AsMemory(), cancellationToken);
        }
        finally
        {
            _sendLock.Release();
        }
    }

    private UdpClient GetAuthenticatedClient()
    {
        if (!_authenticated)
        {
            throw new InvalidOperationException(
                "Dungeon UDP session is not authenticated.");
        }

        return _client ?? throw new InvalidOperationException(
            "Dungeon UDP session is not connected.");
    }

    private static uint NextSequence(ref uint sequence)
    {
        sequence++;
        if (sequence == 0)
        {
            sequence = 1;
        }

        return sequence;
    }
}
