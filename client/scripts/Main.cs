using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;
using Godot;

namespace DnfMockClient;

public partial class Main : Control
{
    private const float DungeonTicksPerSecond = 30.0f;

    private readonly TcpConnectionService _connection = new();
    private readonly DungeonUdpService _udpService = new();
    private readonly object _snapshotLock = new();

    private AuthenticationPanel _authenticationPanel = null!;
    private Control _channelPanel = null!;
    private OptionButton _channelSelect = null!;
    private Button _refreshChannelsButton = null!;
    private Button _joinChannelButton = null!;
    private Control _partyPanel = null!;
    private Button _createPartyButton = null!;
    private LineEdit _partyIdInput = null!;
    private Button _joinPartyButton = null!;
    private Button _leavePartyButton = null!;
    private Button _refreshPartyButton = null!;
    private Label _partyInfoLabel = null!;
    private Control _dungeonPanel = null!;
    private LineEdit _dungeonTemplateInput = null!;
    private Button _enterDungeonButton = null!;
    private Label _statusLabel = null!;
    private RichTextLabel _eventLog = null!;
    private Control _lobbyScreen = null!;
    private Control _dungeonScreen = null!;
    private DungeonWorldView _dungeonWorldView = null!;
    private Label _dungeonTitleLabel = null!;
    private Label _tickLabel = null!;
    private Label _roomLabel = null!;
    private Label _positionLabel = null!;
    private Label _udpStatusLabel = null!;
    private Label _hpLabel = null!;
    private Label _mpLabel = null!;
    private Label _skillLabel = null!;
    private Label _enemyLabel = null!;
    private Control _dungeonResultOverlay = null!;
    private Label _dungeonResultLabel = null!;
    private Button _leaveDungeonButton = null!;

    private CancellationTokenSource? _operationCancellation;
    private bool _busy;
    private bool _loggedIn;
    private bool _joinedChannel;
    private bool _inParty;
    private bool _movementSendInProgress;
    private bool _attackPressed;
    private bool _receivedFirstSnapshot;
    private bool _dungeonFinished;
    private double _movementSendTime;
    private Vector2 _lastDirection = Vector2.Right;
    private ulong _localSessionId;
    private ulong _partyLeaderSessionId;
    private string _gameServerHost = string.Empty;
    private DungeonSnapshotData? _pendingSnapshot;
    private string? _pendingUdpError;

    public override void _Ready()
    {
        FindControls();
        ConnectSignals();
        _udpService.SnapshotReceived += OnSnapshotReceived;
        _udpService.ErrorOccurred += OnUdpError;
        RefreshControls();
    }

    public override void _ExitTree()
    {
        DisconnectSignals();
        _udpService.SnapshotReceived -= OnSnapshotReceived;
        _udpService.ErrorOccurred -= OnUdpError;
        _operationCancellation?.Cancel();
        _operationCancellation?.Dispose();
        _connection.Dispose();
        _udpService.Dispose();
    }

    public override void _PhysicsProcess(double delta)
    {
        if (!_udpService.IsAuthenticated || !_dungeonScreen.Visible ||
            _dungeonFinished)
        {
            return;
        }

        var direction = Vector2.Zero;
        direction.X = Input.IsKeyPressed(Key.A) ? direction.X - 1.0f : direction.X;
        direction.X = Input.IsKeyPressed(Key.D) ? direction.X + 1.0f : direction.X;
        direction.Y = Input.IsKeyPressed(Key.W) ? direction.Y - 1.0f : direction.Y;
        direction.Y = Input.IsKeyPressed(Key.S) ? direction.Y + 1.0f : direction.Y;

        if (direction.LengthSquared() > 1.0f)
        {
            direction = direction.Normalized();
        }

        if (direction != Vector2.Zero)
        {
            _lastDirection = direction;
        }

        _movementSendTime += delta;
        if (_movementSendTime >= 0.05 && !_movementSendInProgress)
        {
            _movementSendTime = 0.0;
            _ = SendMovementAsync(
                direction,
                Input.IsKeyPressed(Key.Space));
        }

        bool attackPressed = Input.IsKeyPressed(Key.J);
        if (attackPressed && !_attackPressed)
        {
            _ = SendAttackAsync();
        }

        _attackPressed = attackPressed;
    }

    public override void _Input(InputEvent inputEvent)
    {
        if (!_udpService.IsAuthenticated || !_dungeonScreen.Visible ||
            _dungeonFinished || inputEvent is not InputEventKey keyEvent ||
            !keyEvent.Pressed || keyEvent.Echo)
        {
            return;
        }

        Vector2 tapDirection = keyEvent.Keycode switch
        {
            Key.A => Vector2.Left,
            Key.D => Vector2.Right,
            Key.W => Vector2.Up,
            Key.S => Vector2.Down,
            _ => Vector2.Zero
        };

        if (tapDirection != Vector2.Zero)
        {
            _lastDirection = tapDirection;
            if (!_movementSendInProgress)
            {
                _ = SendMovementAsync(tapDirection, false);
            }
        }

        if (keyEvent.Keycode == Key.J && !_attackPressed)
        {
            _attackPressed = true;
            _ = SendAttackAsync();
        }
    }

    private void FindControls()
    {
        _authenticationPanel = GetNode<AuthenticationPanel>(
            "Margin/RootVBox/Content/LeftScroll/Steps/AuthenticationPanel");
        _channelPanel = GetNode<Control>("%ChannelPanel");
        _channelSelect = GetNode<OptionButton>("%ChannelSelect");
        _refreshChannelsButton = GetNode<Button>("%RefreshChannelsButton");
        _joinChannelButton = GetNode<Button>("%JoinChannelButton");
        _partyPanel = GetNode<Control>("%PartyPanel");
        _createPartyButton = GetNode<Button>("%CreatePartyButton");
        _partyIdInput = GetNode<LineEdit>("%PartyIdInput");
        _joinPartyButton = GetNode<Button>("%JoinPartyButton");
        _leavePartyButton = GetNode<Button>("%LeavePartyButton");
        _refreshPartyButton = GetNode<Button>("%RefreshPartyButton");
        _partyInfoLabel = GetNode<Label>("%PartyInfoLabel");
        _dungeonPanel = GetNode<Control>("%DungeonPanel");
        _dungeonTemplateInput = GetNode<LineEdit>("%DungeonTemplateInput");
        _enterDungeonButton = GetNode<Button>("%EnterDungeonButton");
        _statusLabel = GetNode<Label>("%StatusLabel");
        _eventLog = GetNode<RichTextLabel>("%EventLog");
        _lobbyScreen = GetNode<Control>("%Margin");
        _dungeonScreen = GetNode<Control>("%DungeonScreen");
        _dungeonWorldView = GetNode<DungeonWorldView>("%DungeonWorldView");
        _dungeonTitleLabel = GetNode<Label>("%DungeonTitleLabel");
        _tickLabel = GetNode<Label>("%TickLabel");
        _roomLabel = GetNode<Label>("%RoomLabel");
        _positionLabel = GetNode<Label>("%PositionLabel");
        _udpStatusLabel = GetNode<Label>("%UdpStatusLabel");
        _hpLabel = GetNode<Label>("%HpLabel");
        _mpLabel = GetNode<Label>("%MpLabel");
        _skillLabel = GetNode<Label>("%SkillLabel");
        _enemyLabel = GetNode<Label>("%EnemyLabel");
        _dungeonResultOverlay = GetNode<Control>("%DungeonResultOverlay");
        _dungeonResultLabel = GetNode<Label>("%DungeonResultLabel");
        _leaveDungeonButton = GetNode<Button>("%LeaveDungeonButton");
    }

    private void ConnectSignals()
    {
        _authenticationPanel.GameConnectionReady +=
            OnGameConnectionReady;
        _refreshChannelsButton.Pressed += OnRefreshChannelsButtonPressed;
        _joinChannelButton.Pressed += OnJoinChannelButtonPressed;
        _createPartyButton.Pressed += OnCreatePartyButtonPressed;
        _joinPartyButton.Pressed += OnJoinPartyButtonPressed;
        _leavePartyButton.Pressed += OnLeavePartyButtonPressed;
        _refreshPartyButton.Pressed += OnRefreshPartyButtonPressed;
        _enterDungeonButton.Pressed += OnEnterDungeonButtonPressed;
        _leaveDungeonButton.Pressed += OnLeaveDungeonButtonPressed;
    }

    private void DisconnectSignals()
    {
        _authenticationPanel.GameConnectionReady -=
            OnGameConnectionReady;
        _refreshChannelsButton.Pressed -= OnRefreshChannelsButtonPressed;
        _joinChannelButton.Pressed -= OnJoinChannelButtonPressed;
        _createPartyButton.Pressed -= OnCreatePartyButtonPressed;
        _joinPartyButton.Pressed -= OnJoinPartyButtonPressed;
        _leavePartyButton.Pressed -= OnLeavePartyButtonPressed;
        _refreshPartyButton.Pressed -= OnRefreshPartyButtonPressed;
        _enterDungeonButton.Pressed -= OnEnterDungeonButtonPressed;
        _leaveDungeonButton.Pressed -= OnLeaveDungeonButtonPressed;
    }

    private async void OnGameConnectionReady(
        AuthCharacterSelectionResponse selection)
    {
        if (_busy)
        {
            SetStatus("다른 게임 서버 요청이 진행 중입니다.", Colors.Orange);
            return;
        }

        BeginOperation(TimeSpan.FromSeconds(5));
        _connection.Disconnect();
        ResetSessionState();
        _gameServerHost = selection.GameServerHost;
        SetStatus(
            $"{selection.GameServerHost}:{selection.GameServerPort} 연결 중...",
            Colors.LightSkyBlue);

        try
        {
            if (selection.ExpiresAtUnix <=
                DateTimeOffset.UtcNow.ToUnixTimeSeconds())
            {
                throw new InvalidDataException("인증 티켓이 만료되었습니다.");
            }

            await _connection.ConnectAsync(
                selection.GameServerHost,
                selection.GameServerPort,
                _operationCancellation!.Token);
            AddLog(
                $"게임 서버 연결: {selection.GameServerHost}:" +
                selection.GameServerPort);

            TcpPacket response = await _connection.SendRequestAsync(
                TcpPacketType.LoginRequest,
                GamePayloadCodec.EncodeLoginRequest(selection.AuthTicket),
                _operationCancellation.Token);
            LoginResponseData login =
                GamePayloadCodec.DecodeLoginResponse(response.Payload);

            if (login.Result != LoginResult.Success)
            {
                _connection.Disconnect();
                ResetSessionState();
                SetStatus(
                    $"게임 서버 로그인 실패: {login.Result}",
                    Colors.Orange);
                AddLog($"LoginResponse: {login.Result}");
                return;
            }

            _loggedIn = true;
            _localSessionId = login.SessionId;
            SetStatus("게임 서버 로그인 성공", Colors.LightGreen);
            AddLog($"게임 로그인 성공: session={_localSessionId}");
            await LoadChannelsAsync(_operationCancellation.Token);
            await LoadDungeonCatalogAsync(_operationCancellation.Token);
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            EndOperation();
        }
    }

    private async void OnRefreshChannelsButtonPressed()
    {
        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            await LoadChannelsAsync(_operationCancellation!.Token);
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            EndOperation();
        }
    }

    private async void OnJoinChannelButtonPressed()
    {
        if (_channelSelect.Selected < 0)
        {
            SetStatus("입장할 채널을 선택해 주세요.", Colors.Orange);
            return;
        }

        uint channelId = (uint)_channelSelect.GetSelectedId();
        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            TcpPacket packet = await _connection.SendRequestAsync(
                TcpPacketType.JoinChannelRequest,
                GamePayloadCodec.EncodeJoinChannelRequest(channelId),
                _operationCancellation!.Token);
            JoinChannelResponse response =
                GamePayloadCodec.DecodeJoinChannelResponse(packet.Payload);

            if (response.Result != JoinChannelResult.Success)
            {
                SetStatus($"채널 입장 실패: {response.Result}", Colors.Orange);
                AddLog($"JoinChannelResponse: {response.Result}");
                return;
            }

            _joinedChannel = true;
            SetStatus($"채널 {response.ChannelId} 입장 성공", Colors.LightGreen);
            AddLog($"채널 {response.ChannelId} 입장 성공");
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            EndOperation();
        }
    }

    private async void OnEnterDungeonButtonPressed()
    {
        if (_partyLeaderSessionId == 0)
        {
            SetStatus("파티 정보를 먼저 새로고침해 주세요.", Colors.Orange);
            return;
        }

        uint templateId = 0;
        if (IsLocalPartyLeader &&
            (!uint.TryParse(_dungeonTemplateInput.Text, out templateId) ||
             templateId == 0))
        {
            SetStatus("던전 템플릿 ID를 확인해 주세요.", Colors.Orange);
            return;
        }

        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            if (IsLocalPartyLeader)
            {
                await CreateDungeonAndConnectAsync(
                    templateId,
                    _operationCancellation!.Token);
            }
            else
            {
                await JoinCreatedDungeonAsync(_operationCancellation!.Token);
            }
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            EndOperation();
        }
    }

    private async void OnCreatePartyButtonPressed()
    {
        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            TcpPacket packet = await _connection.SendRequestAsync(
                TcpPacketType.CreatePartyRequest,
                GamePayloadCodec.EncodeCreatePartyRequest(),
                _operationCancellation!.Token);
            CreatePartyResponse response =
                GamePayloadCodec.DecodeCreatePartyResponse(packet.Payload);

            if (response.Result != CreatePartyResult.Success)
            {
                SetStatus($"파티 생성 실패: {response.Result}", Colors.Orange);
                AddLog($"CreatePartyResponse: {response.Result}");
                return;
            }

            _inParty = true;
            PartySnapshotData? snapshot = await LoadPartySnapshotAsync(
                _operationCancellation.Token);
            if (snapshot is null)
            {
                SetStatus("파티를 생성했지만 정보를 조회하지 못했습니다.", Colors.Orange);
                return;
            }

            SetStatus($"파티 {response.PartyId} 생성 성공", Colors.LightGreen);
            AddLog(
                $"파티 생성: id={response.PartyId}, leader={response.LeaderSessionId}");
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            EndOperation();
        }
    }

    private async void OnJoinPartyButtonPressed()
    {
        if (!ulong.TryParse(_partyIdInput.Text, out ulong partyId) || partyId == 0)
        {
            SetStatus("가입할 Party ID를 확인해 주세요.", Colors.Orange);
            return;
        }

        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            TcpPacket packet = await _connection.SendRequestAsync(
                TcpPacketType.JoinPartyRequest,
                GamePayloadCodec.EncodeJoinPartyRequest(partyId),
                _operationCancellation!.Token);
            JoinPartyResponse response =
                GamePayloadCodec.DecodeJoinPartyResponse(packet.Payload);

            if (response.Result != JoinPartyResult.Success)
            {
                SetStatus($"파티 가입 실패: {response.Result}", Colors.Orange);
                AddLog($"JoinPartyResponse: {response.Result}");
                return;
            }

            _inParty = true;
            PartySnapshotData? snapshot = await LoadPartySnapshotAsync(
                _operationCancellation.Token);
            if (snapshot is null)
            {
                SetStatus("파티에 가입했지만 정보를 조회하지 못했습니다.", Colors.Orange);
                return;
            }

            SetStatus($"파티 {response.PartyId} 가입 성공", Colors.LightGreen);
            AddLog(
                $"파티 가입: id={response.PartyId}, leader={response.LeaderSessionId}");
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            EndOperation();
        }
    }

    private void ShowPartyInfo(PartySnapshotData snapshot)
    {
        _partyLeaderSessionId = snapshot.LeaderSessionId;
        _partyInfoLabel.Text =
            $"Party {snapshot.PartyId} / Leader {snapshot.LeaderSessionId}\n" +
            $"Members: {string.Join(", ", snapshot.Members)}";
    }

    private async void OnRefreshPartyButtonPressed()
    {
        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            PartySnapshotData? snapshot = await LoadPartySnapshotAsync(
                _operationCancellation!.Token);
            if (snapshot is null)
            {
                SetStatus("현재 가입한 파티가 없습니다.", Colors.Orange);
                return;
            }

            SetStatus($"파티 {snapshot.PartyId} 정보 갱신", Colors.LightGreen);
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            EndOperation();
        }
    }

    private async void OnLeavePartyButtonPressed()
    {
        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            TcpPacket packet = await _connection.SendRequestAsync(
                TcpPacketType.LeavePartyRequest,
                GamePayloadCodec.EncodeLeavePartyRequest(),
                _operationCancellation!.Token);
            LeavePartyResult result =
                GamePayloadCodec.DecodeLeavePartyResponse(packet.Payload);

            if (result != LeavePartyResult.Success)
            {
                SetStatus($"파티 탈퇴 실패: {result}", Colors.Orange);
                AddLog($"LeavePartyResponse: {result}");
                return;
            }

            ClearPartyInfo();
            SetStatus("파티 탈퇴 성공", Colors.LightGreen);
            AddLog("파티 탈퇴 성공");
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            EndOperation();
        }
    }

    private void ClearPartyInfo()
    {
        _inParty = false;
        _partyLeaderSessionId = 0;
        _partyIdInput.Clear();
        _partyInfoLabel.Text = "파티 없음";
    }

    private async Task<PartySnapshotData?> LoadPartySnapshotAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await _connection.SendRequestAsync(
            TcpPacketType.PartySnapshotRequest,
            GamePayloadCodec.EncodePartySnapshotRequest(),
            cancellationToken);
        PartySnapshotData snapshot =
            GamePayloadCodec.DecodePartySnapshotResponse(packet.Payload);

        if (snapshot.Result != PartySnapshotResult.Success)
        {
            ClearPartyInfo();
            AddLog($"PartySnapshotResponse: {snapshot.Result}");
            return null;
        }

        _inParty = true;
        ShowPartyInfo(snapshot);
        AddLog(
            $"파티 정보: id={snapshot.PartyId}, members={snapshot.Members.Count}");
        return snapshot;
    }

    private async Task LoadChannelsAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await _connection.SendRequestAsync(
            TcpPacketType.ChannelListRequest,
            GamePayloadCodec.EncodeChannelListRequest(),
            cancellationToken);
        IReadOnlyList<ChannelInfo> channels =
            GamePayloadCodec.DecodeChannelListResponse(packet.Payload);

        _channelSelect.Clear();
        foreach (ChannelInfo channel in channels)
        {
            _channelSelect.AddItem(
                $"{channel.DisplayName}  ({channel.CurrentPlayers}/{channel.MaxPlayers})",
                checked((int)channel.Id));
        }

        AddLog($"채널 목록 수신: {channels.Count}개");
    }

    private async Task LoadDungeonCatalogAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await _connection.SendRequestAsync(
            TcpPacketType.DungeonCatalogRequest,
            GamePayloadCodec.EncodeDungeonCatalogRequest(),
            cancellationToken);
        DungeonCatalogData catalog =
            GamePayloadCodec.DecodeDungeonCatalogResponse(packet.Payload);

        if (catalog.Result != CatalogResult.Success)
        {
            AddLog($"DungeonCatalogResponse: {catalog.Result}");
            return;
        }

        foreach (DungeonCatalogEntry dungeon in catalog.Dungeons)
        {
            if (dungeon.Available)
            {
                _dungeonTemplateInput.Text = dungeon.TemplateId.ToString();
                break;
            }
        }

        AddLog($"던전 카탈로그 수신: {catalog.Dungeons.Count}개");
    }

    private async Task<DungeonStaticData> LoadDungeonStaticDataAsync(
        ulong dungeonId,
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await _connection.SendRequestAsync(
            TcpPacketType.DungeonStaticDataRequest,
            DungeonStaticDataCodec.EncodeRequest(dungeonId),
            cancellationToken);
        DungeonStaticData staticData =
            DungeonStaticDataCodec.DecodeResponse(packet.Payload);

        AddLog(
            $"던전 정적 데이터 수신: rooms={staticData.Rooms.Count}, " +
            $"enemies={staticData.EnemyTemplates.Count}");
        return staticData;
    }

    private void BeginOperation(TimeSpan timeout)
    {
        _operationCancellation?.Dispose();
        _operationCancellation = new CancellationTokenSource(timeout);
        _busy = true;
        RefreshControls();
    }

    private void EndOperation()
    {
        _operationCancellation?.Dispose();
        _operationCancellation = null;
        _busy = false;

        if (IsInsideTree())
        {
            RefreshControls();
        }
    }

    private void ResetSessionState()
    {
        _udpService.Disconnect();
        _dungeonWorldView.ClearStaticData();
        _loggedIn = false;
        _joinedChannel = false;
        _localSessionId = 0;
        _gameServerHost = string.Empty;
        _receivedFirstSnapshot = false;
        _channelSelect.Clear();
        ClearPartyInfo();
        _lobbyScreen.Visible = true;
        _dungeonScreen.Visible = false;
    }

    private void RefreshControls()
    {
        _channelSelect.Disabled = !_loggedIn || _joinedChannel || _busy;
        _refreshChannelsButton.Disabled = !_loggedIn || _joinedChannel || _busy;
        _joinChannelButton.Disabled = !_loggedIn || _joinedChannel ||
            _channelSelect.ItemCount == 0 || _busy;
        _channelPanel.Modulate = _loggedIn ? Colors.White : Colors.DimGray;

        _createPartyButton.Disabled = !_joinedChannel || _inParty || _busy;
        _partyIdInput.Editable = _joinedChannel && !_inParty && !_busy;
        _joinPartyButton.Disabled = !_joinedChannel || _inParty || _busy;
        _leavePartyButton.Disabled = !_inParty || _busy;
        _refreshPartyButton.Disabled = !_inParty || _busy;
        _partyPanel.Modulate = _joinedChannel ? Colors.White : Colors.DimGray;

        _dungeonTemplateInput.Editable = IsLocalPartyLeader && !_busy;
        _enterDungeonButton.Disabled = !_inParty ||
            _partyLeaderSessionId == 0 || _busy;
        _enterDungeonButton.Text = !_inParty
            ? "던전 입장"
            : IsLocalPartyLeader
                ? "던전 생성 및 입장"
                : "생성된 던전 참가";
        _dungeonPanel.Modulate = _inParty ? Colors.White : Colors.DimGray;
    }

    private void ShowOperationError(Exception exception)
    {
        if (!IsInsideTree())
        {
            return;
        }

        string message = exception switch
        {
            OperationCanceledException => "요청 시간이 초과되었습니다.",
            SocketException socket => $"네트워크 오류: {socket.SocketErrorCode}",
            _ => exception.Message
        };

        SetStatus(message, Colors.OrangeRed);
        AddLog($"오류: {message}");

        if (exception is IOException or SocketException)
        {
            _connection.Disconnect();
            ResetSessionState();
        }
    }

    private static bool IsExpectedOperationError(Exception exception)
    {
        return exception is OperationCanceledException or SocketException or
            IOException or InvalidDataException or ArgumentException or
            InvalidOperationException or OverflowException;
    }

    private void SetStatus(string message, Color color)
    {
        _statusLabel.Text = message;
        _statusLabel.Modulate = color;
    }

    private void AddLog(string message)
    {
        _eventLog.AppendText($"\n[color=#aeb9cc]{DateTime.Now:HH:mm:ss}[/color] {message}");
    }

    private bool IsLocalPartyLeader =>
        _inParty && _partyLeaderSessionId == _localSessionId;

    private async Task CreateDungeonAndConnectAsync(
        uint templateId,
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await _connection.SendRequestAsync(
            TcpPacketType.EnterDungeonRequest,
            GamePayloadCodec.EncodeEnterDungeonRequest(templateId),
            cancellationToken);
        EnterDungeonResponse response =
            GamePayloadCodec.DecodeEnterDungeonResponse(packet.Payload);

        if (response.Result != EnterDungeonResult.Success)
        {
            SetStatus($"던전 생성 실패: {response.Result}", Colors.Orange);
            AddLog($"EnterDungeonResponse: {response.Result}");
            return;
        }

        SetStatus($"던전 {response.DungeonId} 생성 성공", Colors.LightGreen);
        AddLog($"던전 생성: id={response.DungeonId}, UDP={response.UdpPort}");

        await ConnectToDungeonAsync(
            response.DungeonId,
            response.UdpPort,
            response.UdpToken,
            cancellationToken);
    }

    private async Task JoinCreatedDungeonAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await _connection.SendRequestAsync(
            TcpPacketType.DungeonConnectionInfoRequest,
            GamePayloadCodec.EncodeDungeonConnectionInfoRequest(),
            cancellationToken);
        DungeonConnectionInfo response =
            GamePayloadCodec.DecodeDungeonConnectionInfoResponse(packet.Payload);

        if (response.Result != DungeonConnectionInfoResult.Success)
        {
            string message = response.Result ==
                DungeonConnectionInfoResult.DungeonNotFound
                ? "파티장이 아직 던전을 생성하지 않았습니다."
                : $"던전 참가 실패: {response.Result}";
            SetStatus(message, Colors.Orange);
            AddLog($"DungeonConnectionInfoResponse: {response.Result}");
            return;
        }

        SetStatus($"던전 {response.DungeonId} 접속 정보 수신", Colors.LightGreen);
        AddLog(
            $"던전 참가 정보: id={response.DungeonId}, UDP={response.UdpPort}");

        await ConnectToDungeonAsync(
            response.DungeonId,
            response.UdpPort,
            response.UdpToken,
            cancellationToken);
    }

    private async Task ConnectToDungeonAsync(
        ulong dungeonId,
        ushort udpPort,
        ulong udpToken,
        CancellationToken cancellationToken)
    {
        DungeonStaticData staticData = await LoadDungeonStaticDataAsync(
            dungeonId,
            cancellationToken);
        if (staticData.Result != DungeonStaticDataResult.Success)
        {
            SetStatus(
                $"던전 정적 데이터 조회 실패: {staticData.Result}",
                Colors.Orange);
            return;
        }

        _dungeonWorldView.SetStaticData(staticData);
        await StartDungeonUdpAsync(
            dungeonId,
            udpPort,
            udpToken,
            cancellationToken);
    }

    private async Task StartDungeonUdpAsync(
        ulong dungeonId,
        ushort udpPort,
        ulong udpToken,
        CancellationToken cancellationToken)
    {
        _receivedFirstSnapshot = false;
        _dungeonFinished = false;
        _dungeonWorldView.SetLocalSessionId(_localSessionId);
        _dungeonTitleLabel.Text = $"Dungeon {dungeonId}";
        _tickLabel.Text = "Tick: -";
        _roomLabel.Text = "Room: -";
        _positionLabel.Text = "Position: -";
        _hpLabel.Text = "HP: -";
        _mpLabel.Text = "MP: -";
        _skillLabel.Text = "Ice Slash: 준비";
        _enemyLabel.Text = "Enemies: -";
        _dungeonResultOverlay.Visible = false;
        _udpStatusLabel.Text = "UDP Hello 전송";

        UdpHelloAckData ack = await _udpService.ConnectAsync(
            _gameServerHost,
            udpPort,
            dungeonId,
            _localSessionId,
            udpToken,
            cancellationToken);

        _udpStatusLabel.Text = $"UDP 인증 성공 (Tick {ack.ServerTick})";
        _lobbyScreen.Visible = false;
        _dungeonScreen.Visible = true;
        AddLog(
            $"UDP 인증 성공: session={_localSessionId}, " +
            $"tick={ack.ServerTick}");
    }

    private void OnLeaveDungeonButtonPressed()
    {
        _udpService.Disconnect();
        _dungeonWorldView.ClearStaticData();
        _dungeonFinished = false;
        _dungeonResultOverlay.Visible = false;
        _dungeonScreen.Visible = false;
        _lobbyScreen.Visible = true;
        SetStatus("던전 화면을 닫았습니다.", Colors.LightGray);
        AddLog("UDP 연결 종료");
    }

    private void OnSnapshotReceived(DungeonSnapshotData snapshot)
    {
        lock (_snapshotLock)
        {
            _pendingSnapshot = snapshot;
        }

        Callable.From(ApplyPendingUdpData).CallDeferred();
    }

    private void OnUdpError(Exception exception)
    {
        lock (_snapshotLock)
        {
            _pendingUdpError = exception.Message;
        }

        Callable.From(ApplyPendingUdpData).CallDeferred();
    }

    private void ApplyPendingUdpData()
    {
        DungeonSnapshotData? snapshot;
        string? error;

        lock (_snapshotLock)
        {
            snapshot = _pendingSnapshot;
            error = _pendingUdpError;
            _pendingSnapshot = null;
            _pendingUdpError = null;
        }

        if (error is not null)
        {
            _udpStatusLabel.Text = $"UDP 오류: {error}";
            AddLog($"UDP 오류: {error}");
        }

        if (snapshot is null)
        {
            return;
        }

        _dungeonWorldView.SetSnapshot(snapshot);
        _tickLabel.Text = $"Tick: {snapshot.ServerTick}";
        _udpStatusLabel.Text = snapshot.State == DungeonStateData.Finished
            ? "던전 완료"
            : "UDP 수신 중";

        uint currentRoomId = 0;

        foreach (PlayerSnapshotData player in snapshot.Players)
        {
            if (player.SessionId != _localSessionId)
            {
                continue;
            }

            currentRoomId = player.RoomId;
            _roomLabel.Text = $"Room: {player.RoomId}";
            _positionLabel.Text =
                $"Position: ({player.X:0.0}, {player.Y:0.0}, {player.Z:0.0})";
            _hpLabel.Text = $"HP {player.CurrentHp} / {player.MaxHp}";
            _hpLabel.Modulate = player.Alive
                ? Colors.LightGreen
                : Colors.IndianRed;
            _mpLabel.Text = $"MP {player.CurrentMp} / {player.MaxMp}";

            uint cooldownTicks = player.RemainingCooldown(1001);
            string cooldownText = cooldownTicks == 0
                ? "준비"
                : $"{cooldownTicks / DungeonTicksPerSecond:0.0}s";
            _skillLabel.Text = player.SkillPhase == SkillPhaseData.Idle
                ? $"Ice Slash: {cooldownText}"
                : $"Ice Slash: {player.SkillPhase} " +
                  $"({player.SkillRemainingTicks}t)";
            break;
        }

        int livingEnemyCount = 0;
        foreach (EnemySnapshotData enemy in snapshot.Enemies)
        {
            if (enemy.Alive && enemy.RoomId == currentRoomId)
            {
                livingEnemyCount++;
            }
        }

        _enemyLabel.Text = $"Enemies: {livingEnemyCount}";

        if (snapshot.State == DungeonStateData.Finished && !_dungeonFinished)
        {
            _dungeonFinished = true;
            _dungeonResultLabel.Text =
                $"DUNGEON CLEAR\nTick {snapshot.ServerTick}\n" +
                "상단 버튼으로 로비에 돌아갈 수 있습니다.";
            _dungeonResultOverlay.Visible = true;
            AddLog($"던전 클리어: tick={snapshot.ServerTick}");
        }

        if (!_receivedFirstSnapshot)
        {
            _receivedFirstSnapshot = true;
            AddLog("첫 DungeonSnapshot 수신");
        }
    }

    private async Task SendMovementAsync(Vector2 direction, bool jump)
    {
        _movementSendInProgress = true;

        try
        {
            await _udpService.SendMovementAsync(
                direction.X,
                direction.Y,
                jump);
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            OnUdpError(exception);
        }
        finally
        {
            _movementSendInProgress = false;
        }
    }

    private async Task SendAttackAsync()
    {
        try
        {
            await _udpService.SendAttackAsync(
                1001,
                _lastDirection.X,
                _lastDirection.Y);
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            OnUdpError(exception);
        }
    }
}
