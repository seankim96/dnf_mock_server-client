using System;
using System.Net.Sockets;
using System.Threading;
using DnfMockClient.Networking;
using Godot;

namespace DnfMockClient;

public partial class Main : Control
{
    private readonly TcpConnectionService _connection = new();

    private LineEdit _addressInput = null!;
    private LineEdit _portInput = null!;
    private Button _connectButton = null!;
    private Label _statusLabel = null!;
    private CancellationTokenSource? _connectCancellation;

    public override void _Ready()
    {
        _addressInput = GetNode<LineEdit>("%AddressInput");
        _portInput = GetNode<LineEdit>("%PortInput");
        _connectButton = GetNode<Button>("%ConnectButton");
        _statusLabel = GetNode<Label>("%StatusLabel");

        _connectButton.Pressed += OnConnectButtonPressed;
    }

    public override void _ExitTree()
    {
        _connectButton.Pressed -= OnConnectButtonPressed;
        _connectCancellation?.Cancel();
        _connectCancellation?.Dispose();
        _connection.Dispose();
    }

    private async void OnConnectButtonPressed()
    {
        if (_connection.IsConnected)
        {
            _connection.Disconnect();
            _connectButton.Text = "서버에 연결";
            SetStatus("연결을 종료했습니다.", Colors.LightGray);
            return;
        }

        if (!int.TryParse(_portInput.Text, out int port) ||
            port is < 1 or > 65535)
        {
            SetStatus("포트는 1부터 65535 사이의 숫자여야 합니다.", Colors.Orange);
            return;
        }

        string host = _addressInput.Text.Trim();
        _connectButton.Disabled = true;
        SetStatus($"{host}:{port} 연결 중...", Colors.LightSkyBlue);

        _connectCancellation?.Dispose();
        _connectCancellation = new CancellationTokenSource(
            TimeSpan.FromSeconds(3));

        try
        {
            await _connection.ConnectAsync(
                host,
                port,
                _connectCancellation.Token);

            if (!IsInsideTree())
            {
                return;
            }

            _connectButton.Text = "연결 해제";
            SetStatus($"{host}:{port} 연결 성공", Colors.LightGreen);
        }
        catch (OperationCanceledException)
        {
            if (IsInsideTree())
            {
                SetStatus("연결 시간이 초과되었습니다.", Colors.Orange);
            }
        }
        catch (SocketException exception)
        {
            if (IsInsideTree())
            {
                SetStatus(
                    $"연결 실패: {exception.SocketErrorCode}",
                    Colors.OrangeRed);
            }
        }
        catch (ArgumentException exception)
        {
            if (IsInsideTree())
            {
                SetStatus($"입력 오류: {exception.Message}", Colors.Orange);
            }
        }
        finally
        {
            if (IsInsideTree())
            {
                _connectButton.Disabled = false;
            }
        }
    }

    private void SetStatus(string message, Color color)
    {
        _statusLabel.Text = message;
        _statusLabel.Modulate = color;
    }
}
