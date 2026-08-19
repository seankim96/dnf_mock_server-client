using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Sockets;
using System.Threading;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;
using Godot;

namespace DnfMockClient;

public partial class Main : Control
{
    private readonly TcpConnectionService _connection = new();

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
    private Control _dungeonPanel = null!;
    private LineEdit _dungeonTemplateInput = null!;
    private Button _enterDungeonButton = null!;
    private Label _statusLabel = null!;
    private RichTextLabel _eventLog = null!;

    private CancellationTokenSource? _operationCancellation;
    private bool _busy;
    private bool _loggedIn;
    private bool _joinedChannel;

    public override void _Ready()
    {
        FindControls();
        ConnectSignals();
        RefreshControls();
    }

    public override void _ExitTree()
    {
        DisconnectSignals();
        _operationCancellation?.Cancel();
        _operationCancellation?.Dispose();
        _connection.Dispose();
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
        _dungeonPanel = GetNode<Control>("%DungeonPanel");
        _dungeonTemplateInput = GetNode<LineEdit>("%DungeonTemplateInput");
        _enterDungeonButton = GetNode<Button>("%EnterDungeonButton");
        _statusLabel = GetNode<Label>("%StatusLabel");
        _eventLog = GetNode<RichTextLabel>("%EventLog");
    }

    private void ConnectSignals()
    {
        _connectButton.Pressed += OnConnectButtonPressed;
        _loginButton.Pressed += OnLoginButtonPressed;
        _refreshChannelsButton.Pressed += OnRefreshChannelsButtonPressed;
        _joinChannelButton.Pressed += OnJoinChannelButtonPressed;
        _enterDungeonButton.Pressed += OnEnterDungeonButtonPressed;
    }

    private void DisconnectSignals()
    {
        _connectButton.Pressed -= OnConnectButtonPressed;
        _loginButton.Pressed -= OnLoginButtonPressed;
        _refreshChannelsButton.Pressed -= OnRefreshChannelsButtonPressed;
        _joinChannelButton.Pressed -= OnJoinChannelButtonPressed;
        _enterDungeonButton.Pressed -= OnEnterDungeonButtonPressed;
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
            LoginResult result = GamePayloadCodec.DecodeLoginResponse(response.Payload);

            if (result != LoginResult.Success)
            {
                SetStatus($"로그인 실패: {result}", Colors.Orange);
                AddLog($"LoginResponse: {result}");
                return;
            }

            _loggedIn = true;
            SetStatus("로그인 성공", Colors.LightGreen);
            AddLog($"로그인 성공: {_playerNameInput.Text.Trim()}");
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

    private async System.Threading.Tasks.Task LoadChannelsAsync(
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
        _loggedIn = false;
        _joinedChannel = false;
        _channelSelect.Clear();
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

        _dungeonTemplateInput.Editable = _joinedChannel && !_busy;
        _enterDungeonButton.Disabled = !_joinedChannel || _busy;
        _dungeonPanel.Modulate = _joinedChannel ? Colors.White : Colors.DimGray;
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
}
