using System;
using System.IO;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography.X509Certificates;
using System.Threading;
using System.Threading.Tasks;
using DnfMockClient.Protocol;

namespace DnfMockClient.Networking;

public sealed class AuthTlsConnectionService : IDisposable
{
    private TcpClient? _client;
    private SslStream? _stream;
    private readonly SemaphoreSlim _requestLock = new(1, 1);
    private readonly TcpReceiveBuffer _receiveBuffer = new();
    private uint _nextRequestId = 1;

    public bool IsConnected =>
        _client?.Connected == true &&
        _stream?.IsAuthenticated == true;

    public async Task ConnectAsync(
        string host,
        int port,
        CancellationToken cancellationToken,
        RemoteCertificateValidationCallback? certificateValidation = null)
    {
        if (string.IsNullOrWhiteSpace(host))
        {
            throw new ArgumentException(
                "Authentication server address is required.",
                nameof(host));
        }

        if (port is < 1 or > 65535)
        {
            throw new ArgumentOutOfRangeException(nameof(port));
        }

        Disconnect();

        var client = new TcpClient();
        SslStream? stream = null;

        try
        {
            await client.ConnectAsync(host, port, cancellationToken);
            stream = new SslStream(
                client.GetStream(),
                leaveInnerStreamOpen: false,
                certificateValidation);

            var authenticationOptions = new SslClientAuthenticationOptions
            {
                TargetHost = host,
                EnabledSslProtocols =
                    SslProtocols.Tls12 | SslProtocols.Tls13,
                CertificateRevocationCheckMode = X509RevocationMode.Online
            };
            await stream.AuthenticateAsClientAsync(
                authenticationOptions,
                cancellationToken);

            _client = client;
            _stream = stream;
            _receiveBuffer.Clear();
            _nextRequestId = 1;
        }
        catch
        {
            stream?.Dispose();
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
            SslStream stream = _stream ?? throw new InvalidOperationException(
                "Authentication TLS client is not connected.");
            TcpPacketType expectedType = GetResponseType(requestType);

            uint requestId = _nextRequestId++;
            byte[] requestBytes = TcpPacketCodec.EncodePacket(
                requestType,
                requestId,
                payload);
            await stream.WriteAsync(requestBytes, cancellationToken);

            TcpPacket response = await ReadPacketAsync(
                stream,
                cancellationToken);

            if (response.Header.RequestId != requestId)
            {
                throw new InvalidDataException(
                    "Authentication response request ID does not match.");
            }

            if (response.Header.Type != expectedType)
            {
                throw new InvalidDataException(
                    "Unexpected authentication response type.");
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
        _stream?.Dispose();
        _stream = null;
        _client?.Dispose();
        _client = null;
        _receiveBuffer.Clear();
    }

    public void Dispose()
    {
        Disconnect();
    }

    private async Task<TcpPacket> ReadPacketAsync(
        SslStream stream,
        CancellationToken cancellationToken)
    {
        var receivedBytes = new byte[4096];

        while (true)
        {
            if (_receiveBuffer.TryPop(out TcpPacket? packet) &&
                packet is not null)
            {
                return packet;
            }

            int receivedSize = await stream.ReadAsync(
                receivedBytes,
                cancellationToken);
            if (receivedSize == 0)
            {
                throw new EndOfStreamException(
                    "Authentication server closed the TLS connection.");
            }

            _receiveBuffer.Append(receivedBytes[..receivedSize]);
        }
    }

    private static TcpPacketType GetResponseType(
        TcpPacketType requestType)
    {
        return requestType switch
        {
            TcpPacketType.AuthLoginRequest =>
                TcpPacketType.AuthLoginResponse,
            TcpPacketType.AuthCharacterListRequest =>
                TcpPacketType.AuthCharacterListResponse,
            TcpPacketType.AuthCharacterSelectionRequest =>
                TcpPacketType.AuthCharacterSelectionResponse,
            _ => throw new ArgumentException(
                "Not an authentication request type.",
                nameof(requestType))
        };
    }
}
