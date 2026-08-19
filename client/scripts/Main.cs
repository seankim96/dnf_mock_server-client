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
    private readonly TcpConnectionService _connection = new();
    private readonly DungeonUdpService _udpService = new();
    private readonly object _snapshotLock = new();

    private LineEdit _addressInput = null!;
    private LineEdit _portInput = null!;
    private Button _connectButton = null!;
    private Control _loginPanel = null!;
    private LineEdit _playerNameInput = null!;
    private Button _loginButton = null!;
    private Control _channelPanel = null!;
    private OptionButton _channelSelect = null!;
    private Button _refreshChannelsButton = null!;
    private Button _joinChannelButton = null!;
    private Control _partyPanel = null!;
    private Button _createPartyButton = null!;
    private LineEdit _partyIdInput = null!;
    private Button _joinPartyButton = null!;
    private Button _leavePartyButton = null!;
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
    private Button _leaveDungeonButton = null!;

    private CancellationTokenSource? _operationCancellation;
    private bool _busy;
    private bool _loggedIn;
    private bool _joinedChannel;
    private bool _inParty;
    private bool _movementSendInProgress;
    private bool _attackPressed;
    private bool _receivedFirstSnapshot;
    private double _movementSendTime;
    private Vector2 _lastDirection = Vector2.Right;
    private ulong _localSessionId;
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
        if (!_udpService.IsRunning || !_dungeonScreen.Visible)
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

    private void FindControls()
    {
        _addressInput = GetNode<LineEdit>("%AddressInput");
        _portInput = GetNode<LineEdit>("%PortInput");
        _connectButton = GetNode<Button>("%ConnectButton");
        _loginPanel = GetNode<Control>("%LoginPanel");
        _playerNameInput = GetNode<LineEdit>("%PlayerNameInput");
        _loginButton = GetNode<Button>("%LoginButton");
        _channelPanel = GetNode<Control>("%ChannelPanel");
        _channelSelect = GetNode<OptionButton>("%ChannelSelect");
        _refreshChannelsButton = GetNode<Button>("%RefreshChannelsButton");
        _joinChannelButton = GetNode<Button>("%JoinChannelButton");
        _partyPanel = GetNode<Control>("%PartyPanel");
        _createPartyButton = GetNode<Button>("%CreatePartyButton");
        _partyIdInput = GetNode<LineEdit>("%PartyIdInput");
        _joinPartyButton = GetNode<Button>("%JoinPartyButton");
        _leavePartyButton = GetNode<Button>("%LeavePartyButton");
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
        _leaveDungeonButton = GetNode<Button>("%LeaveDungeonButton");
    }

    private void ConnectSignals()
    {
        _connectButton.Pressed += OnConnectButtonPressed;
        _loginButton.Pressed += OnLoginButtonPressed;
        _refreshChannelsButton.Pressed += OnRefreshChannelsButtonPressed;
        _joinChannelButton.Pressed += OnJoinChannelButtonPressed;
        _createPartyButton.Pressed += OnCreatePartyButtonPressed;
        _joinPartyButton.Pressed += OnJoinPartyButtonPressed;
        _leavePartyButton.Pressed += OnLeavePartyButtonPressed;
        _enterDungeonButton.Pressed += OnEnterDungeonButtonPressed;
        _leaveDungeonButton.Pressed += OnLeaveDungeonButtonPressed;
    }

    private void DisconnectSignals()
    {
        _connectButton.Pressed -= OnConnectButtonPressed;
        _loginButton.Pressed -= OnLoginButtonPressed;
        _refreshChannelsButton.Pressed -= OnRefreshChannelsButtonPressed;
        _joinChannelButton.Pressed -= OnJoinChannelButtonPressed;
        _createPartyButton.Pressed -= OnCreatePartyButtonPressed;
        _joinPartyButton.Pressed -= OnJoinPartyButtonPressed;
        _leavePartyButton.Pressed -= OnLeavePartyButtonPressed;
        _enterDungeonButton.Pressed -= OnEnterDungeonButtonPressed;
        _leaveDungeonButton.Pressed -= OnLeaveDungeonButtonPressed;
    }

    private async void OnConnectButtonPressed()
    {
        if (_connection.IsConnected)
        {
            _connection.Disconnect();
            ResetSessionState();
            SetStatus("연결을 종료했습니다.", Colors.LightGray);
            AddLog("TCP 연결 종료");
            RefreshControls();
            return;
        }

        if (!int.TryParse(_portInput.Text, out int port) || port is < 1 or > 65535)
        {
            SetStatus("포트는 1부터 65535 사이의 숫자여야 합니다.", Colors.Orange);
            return;
        }

        string host = _addressInput.Text.Trim();
        BeginOperation(TimeSpan.FromSeconds(3));
        SetStatus($"{host}:{port} 연결 중...", Colors.LightSkyBlue);

        try
        {
            await _connection.ConnectAsync(host, port, _operationCancellation!.Token);
            if (!IsInsideTree())
            {
                return;
            }

            SetStatus($"{host}:{port} 연결 성공", Colors.LightGreen);
            AddLog($"TCP 연결 성공: {host}:{port}");
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

    private async void OnLoginButtonPressed()
    {
        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            byte[] payload = GamePayloadCodec.EncodeLoginRequest(
                _playerNameInput.Text.Trim());
            TcpPacket response = await _connection.SendRequestAsync(
                TcpPacketType.LoginRequest,
                payload,
                _operationCancellation!.Token);
            LoginResponseData login =
                GamePayloadCodec.DecodeLoginResponse(response.Payload);

            if (login.Result != LoginResult.Success)
            {
                SetStatus($"로그인 실패: {login.Result}", Colors.Orange);
                AddLog($"LoginResponse: {login.Result}");
                return;
            }

            _loggedIn = true;
            _localSessionId = login.SessionId;
            SetStatus("로그인 성공", Colors.LightGreen);
            AddLog(
                $"로그인 성공: {_playerNameInput.Text.Trim()}, session={_localSessionId}");
            await LoadChannelsAsync(_operationCancellation.Token);
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
        if (!uint.TryParse(_dungeonTemplateInput.Text, out uint templateId) ||
            templateId == 0)
        {
            SetStatus("던전 템플릿 ID를 확인해 주세요.", Colors.Orange);
            return;
        }

        BeginOperation(TimeSpan.FromSeconds(5));

        try
        {
            TcpPacket packet = await _connection.SendRequestAsync(
                TcpPacketType.EnterDungeonRequest,
                GamePayloadCodec.EncodeEnterDungeonRequest(templateId),
                _operationCancellation!.Token);
            EnterDungeonResponse response =
                GamePayloadCodec.DecodeEnterDungeonResponse(packet.Payload);

            if (response.Result != EnterDungeonResult.Success)
            {
                SetStatus($"던전 입장 실패: {response.Result}", Colors.Orange);
                AddLog($"EnterDungeonResponse: {response.Result}");
                return;
            }

            SetStatus($"던전 {response.DungeonId} 생성 성공", Colors.LightGreen);
            AddLog($"던전 생성: id={response.DungeonId}, UDP={response.UdpPort}");

            await StartDungeonUdpAsync(response, _localSessionId,
                _operationCancellation.Token);
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
            ShowPartyInfo(response.PartyId, response.LeaderSessionId);
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
            ShowPartyInfo(response.PartyId, response.LeaderSessionId);
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

    private void ShowPartyInfo(ulong partyId, ulong leaderSessionId)
    {
        _partyInfoLabel.Text =
            $"Party {partyId} / Leader {leaderSessionId}";
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
        _partyIdInput.Clear();
        _partyInfoLabel.Text = "파티 없음";
    }

    private async Task LoadChannelsAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket packet = await _connection.SendRequestAsync(
            TcpPacketType.ChannelListRequest,
            Array.Empty<byte>(),
            cancellationToken);
        IReadOnlyList<ChannelInfo> channels =
            GamePayloadCodec.DecodeChannelListResponse(packet.Payload);

        _channelSelect.Clear();
        foreach (ChannelInfo channel in channels)
        {
            _channelSelect.AddItem(
                $"채널 {channel.Id}  ({channel.CurrentPlayers}/{channel.MaxPlayers})",
                checked((int)channel.Id));
        }

        AddLog($"채널 목록 수신: {channels.Count}개");
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
        _loggedIn = false;
        _joinedChannel = false;
        _localSessionId = 0;
        _receivedFirstSnapshot = false;
        _channelSelect.Clear();
        ClearPartyInfo();
        _lobbyScreen.Visible = true;
        _dungeonScreen.Visible = false;
    }

    private void RefreshControls()
    {
        bool connected = _connection.IsConnected;

        _addressInput.Editable = !connected && !_busy;
        _portInput.Editable = !connected && !_busy;
        _connectButton.Disabled = _busy;
        _connectButton.Text = connected ? "연결 해제" : "서버에 연결";

        _playerNameInput.Editable = connected && !_loggedIn && !_busy;
        _loginButton.Disabled = !connected || _loggedIn || _busy;
        _loginPanel.Modulate = connected ? Colors.White : Colors.DimGray;

        _channelSelect.Disabled = !_loggedIn || _joinedChannel || _busy;
        _refreshChannelsButton.Disabled = !_loggedIn || _joinedChannel || _busy;
        _joinChannelButton.Disabled = !_loggedIn || _joinedChannel ||
            _channelSelect.ItemCount == 0 || _busy;
        _channelPanel.Modulate = _loggedIn ? Colors.White : Colors.DimGray;

        _createPartyButton.Disabled = !_joinedChannel || _inParty || _busy;
        _partyIdInput.Editable = _joinedChannel && !_inParty && !_busy;
        _joinPartyButton.Disabled = !_joinedChannel || _inParty || _busy;
        _leavePartyButton.Disabled = !_inParty || _busy;
        _partyPanel.Modulate = _joinedChannel ? Colors.White : Colors.DimGray;

        _dungeonTemplateInput.Editable = _inParty && !_busy;
        _enterDungeonButton.Disabled = !_inParty || _busy;
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

    private async Task StartDungeonUdpAsync(
        EnterDungeonResponse response,
        ulong sessionId,
        CancellationToken cancellationToken)
    {
        _localSessionId = sessionId;
        _receivedFirstSnapshot = false;
        _dungeonWorldView.SetLocalSessionId(sessionId);
        _dungeonTitleLabel.Text = $"Dungeon {response.DungeonId}";
        _tickLabel.Text = "Tick: -";
        _roomLabel.Text = "Room: -";
        _positionLabel.Text = "Position: -";
        _udpStatusLabel.Text = "UDP Hello 전송";

        await _udpService.ConnectAsync(
            _addressInput.Text.Trim(),
            response.UdpPort,
            response.DungeonId,
            sessionId,
            response.UdpToken,
            cancellationToken);

        _lobbyScreen.Visible = false;
        _dungeonScreen.Visible = true;
        AddLog($"UDP Hello 전송: session={sessionId}");
    }

    private void OnLeaveDungeonButtonPressed()
    {
        _udpService.Disconnect();
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
        _udpStatusLabel.Text = "UDP 수신 중";

        foreach (PlayerSnapshotData player in snapshot.Players)
        {
            if (player.SessionId != _localSessionId)
            {
                continue;
            }

            _roomLabel.Text = $"Room: {player.RoomId}";
            _positionLabel.Text =
                $"Position: ({player.X:0.0}, {player.Y:0.0}, {player.Z:0.0})";
            break;
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
