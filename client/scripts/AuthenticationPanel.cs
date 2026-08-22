using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Threading;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;
using Godot;

namespace DnfMockClient;

public partial class AuthenticationPanel : PanelContainer
{
    private readonly AuthenticationClient _authenticationClient = new();
    private readonly List<AuthCharacterSummary> _characters = new();

    private LineEdit _addressInput = null!;
    private LineEdit _portInput = null!;
    private LineEdit _loginIdInput = null!;
    private LineEdit _passwordInput = null!;
    private LineEdit _fingerprintInput = null!;
    private Button _loginButton = null!;
    private Label _statusLabel = null!;
    private OptionButton _characterSelect = null!;

    private CancellationTokenSource? _operationCancellation;
    private bool _busy;

    public override void _Ready()
    {
        _addressInput = GetNode<LineEdit>("%AuthAddressInput");
        _portInput = GetNode<LineEdit>("%AuthPortInput");
        _loginIdInput = GetNode<LineEdit>("%AuthLoginIdInput");
        _passwordInput = GetNode<LineEdit>("%AuthPasswordInput");
        _fingerprintInput = GetNode<LineEdit>("%AuthFingerprintInput");
        _loginButton = GetNode<Button>("%AuthLoginButton");
        _statusLabel = GetNode<Label>("%AuthStatusLabel");
        _characterSelect = GetNode<OptionButton>("%AuthCharacterSelect");

        _loginButton.Pressed += OnLoginButtonPressed;
        RefreshControls();
    }

    public override void _ExitTree()
    {
        _loginButton.Pressed -= OnLoginButtonPressed;
        _operationCancellation?.Cancel();
        _operationCancellation?.Dispose();
        _authenticationClient.Dispose();
    }

    private async void OnLoginButtonPressed()
    {
        if (!int.TryParse(_portInput.Text, out int port) ||
            port is < 1 or > 65535)
        {
            SetStatus("TLS 포트를 확인해 주세요.", Colors.Orange);
            return;
        }

        string host = _addressInput.Text.Trim();
        string loginId = _loginIdInput.Text.Trim();
        string password = _passwordInput.Text;

        BeginOperation();
        ClearCharacters();
        SetStatus($"{host}:{port} 인증 중...", Colors.LightSkyBlue);

        try
        {
            _authenticationClient.Disconnect();
            RemoteCertificateValidationCallback? certificateValidation =
                CreateCertificateValidation(_fingerprintInput.Text);
            await _authenticationClient.ConnectAsync(
                host,
                port,
                _operationCancellation!.Token,
                certificateValidation);

            AuthLoginResult loginResult =
                await _authenticationClient.LoginAsync(
                    loginId,
                    password,
                    _operationCancellation.Token);
            if (loginResult != AuthLoginResult.Success)
            {
                SetStatus($"인증 실패: {loginResult}", Colors.Orange);
                return;
            }

            AuthCharacterListResponse characterList =
                await _authenticationClient.GetCharactersAsync(
                    _operationCancellation.Token);
            if (characterList.Result != AuthCharacterListResult.Success)
            {
                SetStatus(
                    $"캐릭터 목록 조회 실패: {characterList.Result}",
                    Colors.Orange);
                return;
            }

            ShowCharacters(characterList.Characters);
            SetStatus(
                $"인증 성공: 캐릭터 {characterList.Characters.Count}개",
                Colors.LightGreen);
        }
        catch (Exception exception) when (IsExpectedOperationError(exception))
        {
            ShowOperationError(exception);
        }
        finally
        {
            _passwordInput.Clear();
            EndOperation();
        }
    }

    private void ShowCharacters(
        IReadOnlyList<AuthCharacterSummary> characters)
    {
        ClearCharacters();

        foreach (AuthCharacterSummary character in characters)
        {
            _characters.Add(character);
            _characterSelect.AddItem(
                $"{character.DisplayName}  Lv.{character.Level}");
        }
    }

    private void ClearCharacters()
    {
        _characters.Clear();
        _characterSelect.Clear();
    }

    private void BeginOperation()
    {
        _operationCancellation?.Dispose();
        _operationCancellation =
            new CancellationTokenSource(TimeSpan.FromSeconds(8));
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

    private void RefreshControls()
    {
        _addressInput.Editable = !_busy;
        _portInput.Editable = !_busy;
        _loginIdInput.Editable = !_busy;
        _passwordInput.Editable = !_busy;
        _fingerprintInput.Editable = !_busy;
        _loginButton.Disabled = _busy;
        _characterSelect.Disabled = _busy || _characters.Count == 0;
    }

    private void ShowOperationError(Exception exception)
    {
        if (!IsInsideTree())
        {
            return;
        }

        string message = exception switch
        {
            OperationCanceledException => "인증 요청 시간이 초과되었습니다.",
            AuthenticationException => "TLS 인증에 실패했습니다.",
            SocketException socket =>
                $"네트워크 오류: {socket.SocketErrorCode}",
            _ => exception.Message
        };
        SetStatus(message, Colors.OrangeRed);
        _authenticationClient.Disconnect();
        ClearCharacters();
    }

    private void SetStatus(string message, Color color)
    {
        _statusLabel.Text = message;
        _statusLabel.Modulate = color;
    }

    private static RemoteCertificateValidationCallback?
        CreateCertificateValidation(string fingerprint)
    {
        string expectedFingerprint = fingerprint
            .Replace(":", string.Empty)
            .Replace(" ", string.Empty)
            .Trim();
        if (expectedFingerprint.Length == 0)
        {
            return null;
        }

        if (expectedFingerprint.Length != 64)
        {
            throw new ArgumentException(
                "SHA-256 인증서 지문은 64자리 16진수여야 합니다.");
        }

        foreach (char character in expectedFingerprint)
        {
            if (!Uri.IsHexDigit(character))
            {
                throw new ArgumentException(
                    "SHA-256 인증서 지문은 16진수여야 합니다.");
            }
        }

        return (_, certificate, _, _) =>
            certificate is not null &&
            string.Equals(
                certificate.GetCertHashString(HashAlgorithmName.SHA256),
                expectedFingerprint,
                StringComparison.OrdinalIgnoreCase);
    }

    private static bool IsExpectedOperationError(Exception exception)
    {
        return exception is OperationCanceledException or
            AuthenticationException or SocketException or IOException or
            InvalidDataException or ArgumentException or
            InvalidOperationException;
    }
}
