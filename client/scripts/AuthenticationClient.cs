using System;
using System.Net.Security;
using System.Threading;
using System.Threading.Tasks;
using DnfMockClient.Protocol;

namespace DnfMockClient.Networking;

public sealed class AuthenticationClient : IDisposable
{
    private readonly AuthTlsConnectionService _connection;

    public AuthenticationClient()
        : this(new AuthTlsConnectionService())
    {
    }

    public AuthenticationClient(AuthTlsConnectionService connection)
    {
        _connection = connection ?? throw new ArgumentNullException(
            nameof(connection));
    }

    public bool IsConnected => _connection.IsConnected;

    public Task ConnectAsync(
        string host,
        int port,
        CancellationToken cancellationToken,
        RemoteCertificateValidationCallback? certificateValidation = null)
    {
        return _connection.ConnectAsync(
            host,
            port,
            cancellationToken,
            certificateValidation);
    }

    public async Task<AuthLoginResult> LoginAsync(
        string loginId,
        string password,
        CancellationToken cancellationToken)
    {
        TcpPacket response = await _connection.SendRequestAsync(
            TcpPacketType.AuthLoginRequest,
            AuthPayloadCodec.EncodeLoginRequest(loginId, password),
            cancellationToken);
        return AuthPayloadCodec.DecodeLoginResponse(response.Payload);
    }

    public async Task<AuthCharacterListResponse> GetCharactersAsync(
        CancellationToken cancellationToken)
    {
        TcpPacket response = await _connection.SendRequestAsync(
            TcpPacketType.AuthCharacterListRequest,
            AuthPayloadCodec.EncodeCharacterListRequest(),
            cancellationToken);
        return AuthPayloadCodec.DecodeCharacterListResponse(response.Payload);
    }

    public async Task<AuthCharacterSelectionResponse> SelectCharacterAsync(
        ulong playerId,
        CancellationToken cancellationToken)
    {
        TcpPacket response = await _connection.SendRequestAsync(
            TcpPacketType.AuthCharacterSelectionRequest,
            AuthPayloadCodec.EncodeCharacterSelectionRequest(playerId),
            cancellationToken);
        return AuthPayloadCodec.DecodeCharacterSelectionResponse(
            response.Payload);
    }

    public void Disconnect()
    {
        _connection.Disconnect();
    }

    public void Dispose()
    {
        _connection.Dispose();
    }
}
