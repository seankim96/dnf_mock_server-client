using System.Net.Security;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;

namespace DnfMockClient.LoadTest;

internal readonly record struct DungeonEndpoint(
    ulong DungeonId,
    ushort UdpPort,
    ulong UdpToken);

internal sealed class LoadTestClient : IDisposable
{
    private readonly string _loginId;
    private readonly string _password;
    private readonly TcpConnectionService _gameConnection = new();
    private readonly DungeonUdpService _udpService = new();
    private Exception? _udpError;
    private string _gameServerHost = string.Empty;

    public LoadTestClient(string loginId, string password)
    {
        _loginId = loginId;
        _password = password;
        _udpService.ErrorOccurred += OnUdpError;
    }

    public ulong SessionId { get; private set; }

    public async Task ConnectLobbyAsync(
        LoadTestOptions options,
        AttemptRecorder recorder,
        CancellationToken cancellationToken)
    {
        AuthCharacterSelectionResponse selection;
        using (var authentication = new AuthenticationClient())
        {
            RemoteCertificateValidationCallback? certificateValidation =
                CertificateFingerprint.CreateValidation(
                    options.CertificateFingerprint);
            await recorder.MeasureAsync(
                LoadTestStage.TlsConnect,
                () => authentication.ConnectAsync(
                    options.AuthHost,
                    options.AuthPort,
                    cancellationToken,
                    certificateValidation));

            AuthLoginResult loginResult = await recorder.MeasureAsync(
                LoadTestStage.AuthLogin,
                () => authentication.LoginAsync(
                    _loginId,
                    _password,
                    cancellationToken));
            Ensure(
                loginResult == AuthLoginResult.Success,
                $"authentication failed: {loginResult}");

            AuthCharacterListResponse characters = await recorder.MeasureAsync(
                LoadTestStage.CharacterList,
                () => authentication.GetCharactersAsync(cancellationToken));
            Ensure(
                characters.Result == AuthCharacterListResult.Success &&
                characters.Characters.Count > 0,
                $"character list failed: {characters.Result}");

            selection = await recorder.MeasureAsync(
                LoadTestStage.CharacterSelection,
                () => authentication.SelectCharacterAsync(
                    characters.Characters[0].PlayerId,
                    cancellationToken));
            Ensure(
                selection.Result == AuthCharacterSelectionResult.Success,
                $"character selection failed: {selection.Result}");
        }

        Ensure(
            selection.ExpiresAtUnix > DateTimeOffset.UtcNow.ToUnixTimeSeconds(),
            "authentication ticket expired before use");

        _gameServerHost = selection.GameServerHost;
        await recorder.MeasureAsync(
            LoadTestStage.GameConnect,
            () => _gameConnection.ConnectAsync(
                selection.GameServerHost,
                selection.GameServerPort,
                cancellationToken));

        TcpPacket loginPacket = await recorder.MeasureAsync(
            LoadTestStage.GameLogin,
            () => SendAsync(
                TcpPacketType.LoginRequest,
                GamePayloadCodec.EncodeLoginRequest(selection.AuthTicket),
                cancellationToken));
        LoginResponseData login =
            GamePayloadCodec.DecodeLoginResponse(loginPacket.Payload);
        Ensure(login.Result == LoginResult.Success, $"login failed: {login.Result}");
        SessionId = login.SessionId;

        IReadOnlyList<ChannelInfo> channels = await GetChannelsAsync(
            recorder,
            cancellationToken);
        Ensure(channels.Count > 0, "server returned no channels");

        foreach (ChannelInfo channel in channels
                     .OrderBy(value => value.CurrentPlayers)
                     .ThenBy(value => value.Id))
        {
            if (channel.CurrentPlayers >= channel.MaxPlayers)
            {
                continue;
            }

            TcpPacket channelPacket = await recorder.MeasureAsync(
                LoadTestStage.ChannelJoin,
                () => SendAsync(
                    TcpPacketType.JoinChannelRequest,
                    GamePayloadCodec.EncodeJoinChannelRequest(channel.Id),
                    cancellationToken));
            JoinChannelResponse response =
                GamePayloadCodec.DecodeJoinChannelResponse(channelPacket.Payload);
            if (response.Result == JoinChannelResult.Success)
            {
                return;
            }

            if (response.Result != JoinChannelResult.ChannelFull)
            {
                Ensure(false, $"channel join failed: {response.Result}");
            }
        }

        Ensure(false, "all channels are full");
    }

    public async Task SustainLobbyAsync(
        TimeSpan duration,
        TimeSpan requestTimeout,
        CancellationToken cancellationToken)
    {
        DateTime deadline = DateTime.UtcNow + duration;
        while (DateTime.UtcNow < deadline)
        {
            TimeSpan remaining = deadline - DateTime.UtcNow;
            TimeSpan delay = remaining < TimeSpan.FromSeconds(1)
                ? remaining
                : TimeSpan.FromSeconds(1);
            if (delay > TimeSpan.Zero)
            {
                await Task.Delay(delay, cancellationToken);
            }

            if (DateTime.UtcNow >= deadline)
            {
                return;
            }

            using var probeCancellation =
                CancellationTokenSource.CreateLinkedTokenSource(
                    cancellationToken);
            probeCancellation.CancelAfter(requestTimeout);
            IReadOnlyList<ChannelInfo> channels =
                await GetChannelsWithoutMeasurementAsync(
                    probeCancellation.Token);
            Ensure(channels.Count > 0, "lobby liveness probe returned no channels");
        }
    }

    public async Task<ulong> CreatePartyAsync(
        AttemptRecorder recorder,
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await recorder.MeasureAsync(
            LoadTestStage.PartyCreate,
            () => SendAsync(
                TcpPacketType.CreatePartyRequest,
                GamePayloadCodec.EncodeCreatePartyRequest(),
                cancellationToken));
        CreatePartyResponse response =
            GamePayloadCodec.DecodeCreatePartyResponse(packet.Payload);
        Ensure(
            response.Result == CreatePartyResult.Success,
            $"party creation failed: {response.Result}");
        return response.PartyId;
    }

    public async Task JoinPartyAsync(
        ulong partyId,
        AttemptRecorder recorder,
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await recorder.MeasureAsync(
            LoadTestStage.PartyJoin,
            () => SendAsync(
                TcpPacketType.JoinPartyRequest,
                GamePayloadCodec.EncodeJoinPartyRequest(partyId),
                cancellationToken));
        JoinPartyResponse response =
            GamePayloadCodec.DecodeJoinPartyResponse(packet.Payload);
        Ensure(
            response.Result == JoinPartyResult.Success,
            $"party join failed: {response.Result}");
    }

    public async Task<DungeonEndpoint> CreateDungeonAsync(
        AttemptRecorder recorder,
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await recorder.MeasureAsync(
            LoadTestStage.DungeonCreate,
            () => SendAsync(
                TcpPacketType.EnterDungeonRequest,
                GamePayloadCodec.EncodeEnterDungeonRequest(1001),
                cancellationToken));
        EnterDungeonResponse response =
            GamePayloadCodec.DecodeEnterDungeonResponse(packet.Payload);
        Ensure(
            response.Result == EnterDungeonResult.Success,
            $"dungeon creation failed: {response.Result}");
        return new DungeonEndpoint(
            response.DungeonId,
            response.UdpPort,
            response.UdpToken);
    }

    public async Task<DungeonEndpoint> GetDungeonEndpointAsync(
        AttemptRecorder recorder,
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await recorder.MeasureAsync(
            LoadTestStage.DungeonLookup,
            () => SendAsync(
                TcpPacketType.DungeonConnectionInfoRequest,
                GamePayloadCodec.EncodeDungeonConnectionInfoRequest(),
                cancellationToken));
        DungeonConnectionInfo response =
            GamePayloadCodec.DecodeDungeonConnectionInfoResponse(packet.Payload);
        Ensure(
            response.Result == DungeonConnectionInfoResult.Success,
            $"dungeon lookup failed: {response.Result}");
        return new DungeonEndpoint(
            response.DungeonId,
            response.UdpPort,
            response.UdpToken);
    }

    public async Task ConnectDungeonAsync(
        DungeonEndpoint endpoint,
        AttemptRecorder recorder,
        CancellationToken cancellationToken)
    {
        TcpPacket staticDataPacket = await recorder.MeasureAsync(
            LoadTestStage.StaticData,
            () => SendAsync(
                TcpPacketType.DungeonStaticDataRequest,
                DungeonStaticDataCodec.EncodeRequest(endpoint.DungeonId),
                cancellationToken));
        DungeonStaticData staticData =
            DungeonStaticDataCodec.DecodeResponse(staticDataPacket.Payload);
        Ensure(
            staticData.Result == DungeonStaticDataResult.Success,
            $"static data failed: {staticData.Result}");

        UdpHelloAckData ack = await recorder.MeasureAsync(
            LoadTestStage.UdpHello,
            () => _udpService.ConnectAsync(
                _gameServerHost,
                endpoint.UdpPort,
                endpoint.DungeonId,
                SessionId,
                endpoint.UdpToken,
                cancellationToken));
        Ensure(
            ack.Result == UdpHelloResult.Success,
            $"UDP hello failed: {ack.Result}");
    }

    public async Task SustainDungeonAsync(
        TimeSpan duration,
        CancellationToken cancellationToken)
    {
        DateTime deadline = DateTime.UtcNow + duration;
        while (DateTime.UtcNow < deadline)
        {
            await Task.Delay(TimeSpan.FromMilliseconds(250), cancellationToken);
            if (_udpError is not null)
            {
                throw new InvalidOperationException(
                    "UDP session failed during the sustain phase.",
                    _udpError);
            }

            Ensure(_udpService.IsAuthenticated, "UDP session lost authentication");
        }
    }

    public void Dispose()
    {
        _udpService.ErrorOccurred -= OnUdpError;
        _udpService.Dispose();
        _gameConnection.Dispose();
    }

    private async Task<IReadOnlyList<ChannelInfo>> GetChannelsAsync(
        AttemptRecorder recorder,
        CancellationToken cancellationToken)
    {
        return await recorder.MeasureAsync(
            LoadTestStage.ChannelList,
            async () => await GetChannelsWithoutMeasurementAsync(cancellationToken));
    }

    private async Task<IReadOnlyList<ChannelInfo>> GetChannelsWithoutMeasurementAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket channelsPacket = await SendAsync(
            TcpPacketType.ChannelListRequest,
            GamePayloadCodec.EncodeChannelListRequest(),
            cancellationToken);
        return GamePayloadCodec.DecodeChannelListResponse(channelsPacket.Payload);
    }

    private Task<TcpPacket> SendAsync(
        TcpPacketType type,
        byte[] payload,
        CancellationToken cancellationToken)
    {
        return _gameConnection.SendRequestAsync(type, payload, cancellationToken);
    }

    private void OnUdpError(Exception exception)
    {
        Interlocked.CompareExchange(ref _udpError, exception, null);
    }

    private static void Ensure(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
