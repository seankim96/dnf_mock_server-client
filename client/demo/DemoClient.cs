using System.Net.Security;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;

namespace DnfMockClient.MultiplayerDemo;

internal readonly record struct DungeonEndpoint(
    ulong DungeonId,
    ushort UdpPort,
    ulong UdpToken);

internal sealed class DemoClient : IDisposable
{
    private const uint DemoSkillId = 1001;

    private readonly string _loginId;
    private readonly string _password;
    private readonly int _index;
    private readonly TcpConnectionService _gameConnection = new();
    private readonly DungeonUdpService _udpService = new();
    private readonly object _snapshotLock = new();

    private DungeonSnapshotData? _latestSnapshot;
    private DateTime _nextAttackTime = DateTime.MinValue;
    private string _gameServerHost = string.Empty;

    public DemoClient(string loginId, string password, int index)
    {
        _loginId = loginId;
        _password = password;
        _index = index;
        _udpService.SnapshotReceived += OnSnapshotReceived;
        _udpService.ErrorOccurred += OnUdpError;
    }

    public ulong SessionId { get; private set; }
    public string Name => _loginId;

    public async Task ConnectLobbyAsync(
        string authHost,
        int authPort,
        string certificateFingerprint,
        CancellationToken cancellationToken)
    {
        AuthCharacterSelectionResponse selection;

        using (var authentication = new AuthenticationClient())
        {
            RemoteCertificateValidationCallback? certificateValidation =
                CertificateFingerprint.CreateValidation(
                    certificateFingerprint);
            await authentication.ConnectAsync(
                authHost,
                authPort,
                cancellationToken,
                certificateValidation);

            AuthLoginResult loginResult = await authentication.LoginAsync(
                _loginId,
                _password,
                cancellationToken);
            Ensure(
                loginResult == AuthLoginResult.Success,
                $"authentication failed: {loginResult}");

            AuthCharacterListResponse characters =
                await authentication.GetCharactersAsync(cancellationToken);
            Ensure(
                characters.Result == AuthCharacterListResult.Success &&
                characters.Characters.Count > 0,
                $"character list failed: {characters.Result}");

            selection = await authentication.SelectCharacterAsync(
                characters.Characters[0].PlayerId,
                cancellationToken);
            Ensure(
                selection.Result == AuthCharacterSelectionResult.Success,
                $"character selection failed: {selection.Result}");
        }

        Ensure(
            selection.ExpiresAtUnix >
                DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
            "authentication ticket already expired");

        _gameServerHost = selection.GameServerHost;
        await _gameConnection.ConnectAsync(
            selection.GameServerHost,
            selection.GameServerPort,
            cancellationToken);

        TcpPacket loginPacket = await SendAsync(
            TcpPacketType.LoginRequest,
            GamePayloadCodec.EncodeLoginRequest(selection.AuthTicket),
            cancellationToken);
        LoginResponseData login =
            GamePayloadCodec.DecodeLoginResponse(loginPacket.Payload);
        Ensure(login.Result == LoginResult.Success, $"login failed: {login.Result}");
        SessionId = login.SessionId;

        TcpPacket channelsPacket = await SendAsync(
            TcpPacketType.ChannelListRequest,
            GamePayloadCodec.EncodeChannelListRequest(),
            cancellationToken);
        IReadOnlyList<ChannelInfo> channels =
            GamePayloadCodec.DecodeChannelListResponse(
                channelsPacket.Payload);
        Ensure(channels.Count > 0, "server returned no channels");

        TcpPacket channelPacket = await SendAsync(
            TcpPacketType.JoinChannelRequest,
            GamePayloadCodec.EncodeJoinChannelRequest(channels[0].Id),
            cancellationToken);
        JoinChannelResponse channel =
            GamePayloadCodec.DecodeJoinChannelResponse(channelPacket.Payload);
        Ensure(
            channel.Result == JoinChannelResult.Success,
            $"channel join failed: {channel.Result}");

        Log($"TCP ready / session={SessionId} / channel={channel.ChannelId}");
    }

    public async Task<ulong> CreatePartyAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await SendAsync(
            TcpPacketType.CreatePartyRequest,
            GamePayloadCodec.EncodeCreatePartyRequest(),
            cancellationToken);
        CreatePartyResponse response =
            GamePayloadCodec.DecodeCreatePartyResponse(packet.Payload);
        Ensure(
            response.Result == CreatePartyResult.Success,
            $"party creation failed: {response.Result}");
        Log($"created party {response.PartyId}");
        return response.PartyId;
    }

    public async Task JoinPartyAsync(
        ulong partyId,
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await SendAsync(
            TcpPacketType.JoinPartyRequest,
            GamePayloadCodec.EncodeJoinPartyRequest(partyId),
            cancellationToken);
        JoinPartyResponse response =
            GamePayloadCodec.DecodeJoinPartyResponse(packet.Payload);
        Ensure(
            response.Result == JoinPartyResult.Success,
            $"party join failed: {response.Result}");
        Log($"joined party {response.PartyId}");
    }

    public async Task<PartySnapshotData> GetPartyAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await SendAsync(
            TcpPacketType.PartySnapshotRequest,
            GamePayloadCodec.EncodePartySnapshotRequest(),
            cancellationToken);
        PartySnapshotData response =
            GamePayloadCodec.DecodePartySnapshotResponse(packet.Payload);
        Ensure(
            response.Result == PartySnapshotResult.Success,
            $"party snapshot failed: {response.Result}");
        return response;
    }

    public async Task<DungeonEndpoint> CreateDungeonAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await SendAsync(
            TcpPacketType.EnterDungeonRequest,
            GamePayloadCodec.EncodeEnterDungeonRequest(1001),
            cancellationToken);
        EnterDungeonResponse response =
            GamePayloadCodec.DecodeEnterDungeonResponse(packet.Payload);
        Ensure(
            response.Result == EnterDungeonResult.Success,
            $"dungeon creation failed: {response.Result}");
        Log($"created dungeon {response.DungeonId}");
        return new DungeonEndpoint(
            response.DungeonId,
            response.UdpPort,
            response.UdpToken);
    }

    public async Task<DungeonEndpoint> GetDungeonEndpointAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await SendAsync(
            TcpPacketType.DungeonConnectionInfoRequest,
            GamePayloadCodec.EncodeDungeonConnectionInfoRequest(),
            cancellationToken);
        DungeonConnectionInfo response =
            GamePayloadCodec.DecodeDungeonConnectionInfoResponse(
                packet.Payload);
        Ensure(
            response.Result == DungeonConnectionInfoResult.Success,
            $"dungeon connection lookup failed: {response.Result}");
        return new DungeonEndpoint(
            response.DungeonId,
            response.UdpPort,
            response.UdpToken);
    }

    public async Task ConnectDungeonAsync(
        DungeonEndpoint endpoint,
        CancellationToken cancellationToken)
    {
        TcpPacket staticDataPacket = await SendAsync(
            TcpPacketType.DungeonStaticDataRequest,
            DungeonStaticDataCodec.EncodeRequest(endpoint.DungeonId),
            cancellationToken);
        DungeonStaticData staticData =
            DungeonStaticDataCodec.DecodeResponse(staticDataPacket.Payload);
        Ensure(
            staticData.Result == DungeonStaticDataResult.Success,
            $"static data failed: {staticData.Result}");

        UdpHelloAckData ack = await _udpService.ConnectAsync(
            _gameServerHost,
            endpoint.UdpPort,
            endpoint.DungeonId,
            SessionId,
            endpoint.UdpToken,
            cancellationToken);
        Log($"UDP authenticated / dungeon={endpoint.DungeonId} / tick={ack.ServerTick}");
    }

    public async Task AutoPlayAsync(CancellationToken cancellationToken)
    {
        while (true)
        {
            cancellationToken.ThrowIfCancellationRequested();

            DungeonSnapshotData? snapshot = LatestSnapshot();
            if (snapshot is null || snapshot.State == DungeonStateData.Waiting)
            {
                await Task.Delay(50, cancellationToken);
                continue;
            }

            if (snapshot.State == DungeonStateData.Finished)
            {
                Log($"DUNGEON CLEAR / tick={snapshot.ServerTick}");
                return;
            }

            PlayerSnapshotData? player = snapshot.Players.FirstOrDefault(
                value => value.SessionId == SessionId);
            if (player is null)
            {
                throw new InvalidOperationException(
                    $"[{Name}] local player is missing from snapshot");
            }

            EnemySnapshotData? enemy = snapshot.Enemies.FirstOrDefault(
                value => value.Alive && value.RoomId == player.RoomId);

            float moveX = 1.0f;
            float moveY = 0.0f;

            if (enemy is not null)
            {
                float offsetX = enemy.X - player.X;
                float offsetY = enemy.Y - player.Y;
                float distance = MathF.Sqrt(
                    offsetX * offsetX + offsetY * offsetY);

                if (distance <= 130.0f)
                {
                    moveX = 0.0f;
                    moveY = 0.0f;

                    if (player.Alive && player.CurrentMp >= 20 &&
                        player.SkillPhase == SkillPhaseData.Idle &&
                        player.RemainingCooldown(DemoSkillId) == 0 &&
                        DateTime.UtcNow >= _nextAttackTime)
                    {
                        float directionX = distance > 0.0f
                            ? offsetX / distance
                            : 1.0f;
                        float directionY = distance > 0.0f
                            ? offsetY / distance
                            : 0.0f;
                        await _udpService.SendAttackAsync(
                            DemoSkillId,
                            directionX,
                            directionY,
                            cancellationToken);
                        _nextAttackTime =
                            DateTime.UtcNow + TimeSpan.FromSeconds(3.1);
                    }
                }
                else
                {
                    moveX = offsetX / distance;
                    moveY = offsetY / distance;
                }
            }

            await _udpService.SendMovementAsync(
                moveX,
                moveY,
                false,
                cancellationToken);
            await Task.Delay(50, cancellationToken);
        }
    }

    public void Dispose()
    {
        _udpService.SnapshotReceived -= OnSnapshotReceived;
        _udpService.ErrorOccurred -= OnUdpError;
        _udpService.Dispose();
        _gameConnection.Dispose();
    }

    private Task<TcpPacket> SendAsync(
        TcpPacketType type,
        byte[] payload,
        CancellationToken cancellationToken)
    {
        return _gameConnection.SendRequestAsync(
            type,
            payload,
            cancellationToken);
    }

    private void OnSnapshotReceived(DungeonSnapshotData snapshot)
    {
        lock (_snapshotLock)
        {
            _latestSnapshot = snapshot;
        }
    }

    private void OnUdpError(Exception exception)
    {
        Log($"UDP error: {exception.Message}");
    }

    private DungeonSnapshotData? LatestSnapshot()
    {
        lock (_snapshotLock)
        {
            return _latestSnapshot;
        }
    }

    private void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException($"[{Name}] {message}");
        }
    }

    private void Log(string message)
    {
        Console.WriteLine($"[{_index + 1}:{Name}] {message}");
    }
}
