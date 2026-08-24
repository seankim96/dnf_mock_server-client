using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using Dnf.Protocol;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;
using Google.FlatBuffers;
using Xunit;
using static DnfMockClient.Tests.TestAssertions;
using static DnfMockClient.Tests.TestPayloads;
using AuthSchema = Dnf.Protocol.Auth;
using TcpSchema = Dnf.Protocol.Tcp;

namespace DnfMockClient.Tests;

[Collection(SocketTestCollection.Name)]
public sealed class AuthTlsConnectionServiceTests
{
    [Fact]
    public async Task ExchangesAuthenticationRequestsOverTls()
    {
        using RSA key = RSA.Create(2048);
        var certificateRequest = new CertificateRequest(
            "CN=localhost",
            key,
            HashAlgorithmName.SHA256,
            RSASignaturePadding.Pkcs1);
        certificateRequest.CertificateExtensions.Add(
            new X509BasicConstraintsExtension(false, false, 0, false));
        certificateRequest.CertificateExtensions.Add(
            new X509KeyUsageExtension(
                X509KeyUsageFlags.DigitalSignature |
                X509KeyUsageFlags.KeyEncipherment,
                false));
        certificateRequest.CertificateExtensions.Add(
            new X509SubjectKeyIdentifierExtension(
                certificateRequest.PublicKey,
                false));
        using X509Certificate2 certificate =
            certificateRequest.CreateSelfSigned(
                DateTimeOffset.UtcNow.AddMinutes(-1),
                DateTimeOffset.UtcNow.AddMinutes(5));

        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();
        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        Task<TcpClient> acceptTask =
            listener.AcceptTcpClientAsync(timeout.Token).AsTask();

        var connection = new AuthTlsConnectionService();
        using var authenticationClient = new AuthenticationClient(connection);
        Task connectTask = authenticationClient.ConnectAsync(
            "localhost",
            port,
            timeout.Token,
            (_, serverCertificate, _, _) =>
                serverCertificate?.GetCertHashString(HashAlgorithmName.SHA256) ==
                certificate.GetCertHashString(HashAlgorithmName.SHA256));

        using TcpClient acceptedClient = await acceptTask;
        using var serverStream = new SslStream(
            acceptedClient.GetStream(),
            leaveInnerStreamOpen: false);
        await serverStream.AuthenticateAsServerAsync(
            certificate,
            clientCertificateRequired: false,
            SslProtocols.Tls12 | SslProtocols.Tls13,
            checkCertificateRevocation: false);
        await connectTask;

        Assert(authenticationClient.IsConnected,
            "Authentication TLS connection was not established.");

        bool gamePacketRejected = false;
        try
        {
            await connection.SendRequestAsync(
                TcpPacketType.LoginRequest,
                Array.Empty<byte>(),
                timeout.Token);
        }
        catch (ArgumentException)
        {
            gamePacketRejected = true;
        }
        Assert(gamePacketRejected,
            "Authentication TLS service accepted a game server packet type.");

        byte[] requestPayload = { 1, 2, 3 };
        Task<TcpPacket> responseTask = connection.SendRequestAsync(
            TcpPacketType.AuthLoginRequest,
            requestPayload,
            timeout.Token);

        var headerBytes = new byte[TcpPacketCodec.HeaderSize];
        await serverStream.ReadExactlyAsync(headerBytes, timeout.Token);
        TcpPacketHeader requestHeader = TcpPacketCodec.DecodeHeader(headerBytes);
        var receivedPayload = new byte[
            requestHeader.PacketSize - TcpPacketCodec.HeaderSize];
        await serverStream.ReadExactlyAsync(receivedPayload, timeout.Token);

        Assert(requestHeader.Type == TcpPacketType.AuthLoginRequest,
            "Authentication TLS request type is incorrect.");
        Assert(receivedPayload.SequenceEqual(requestPayload),
            "Authentication TLS request payload is incorrect.");

        byte[] responseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.AuthLoginResponse,
            requestHeader.RequestId,
            new byte[] { 4, 5, 6 });
        await serverStream.WriteAsync(responseBytes, timeout.Token);

        TcpPacket response = await responseTask;
        Assert(response.Header.Type == TcpPacketType.AuthLoginResponse,
            "Authentication TLS response type is incorrect.");
        Assert(response.Payload.SequenceEqual(new byte[] { 4, 5, 6 }),
            "Authentication TLS response payload is incorrect.");

        Task<AuthLoginResult> loginTask = authenticationClient.LoginAsync(
            "account_1",
            "password",
            timeout.Token);

        await serverStream.ReadExactlyAsync(headerBytes, timeout.Token);
        TcpPacketHeader loginHeader = TcpPacketCodec.DecodeHeader(headerBytes);
        var loginPayload = new byte[
            loginHeader.PacketSize - TcpPacketCodec.HeaderSize];
        await serverStream.ReadExactlyAsync(loginPayload, timeout.Token);

        var loginBuffer = new ByteBuffer(loginPayload);
        Assert(AuthSchema.AuthMessage.VerifyAuthMessage(loginBuffer),
            "Authentication client sent an invalid FlatBuffer.");
        AuthSchema.AuthMessage loginMessage =
            AuthSchema.AuthMessage.GetRootAsAuthMessage(loginBuffer);
        AuthSchema.LoginRequest loginRequest =
            loginMessage.PayloadAsLoginRequest();
        Assert(loginHeader.Type == TcpPacketType.AuthLoginRequest &&
            loginMessage.PayloadType == AuthSchema.AuthPayload.LoginRequest &&
            loginRequest.LoginId == "account_1" &&
            loginRequest.Password == "password",
            "Authentication client login request is incorrect.");

        var loginResponseBuilder = new FlatBufferBuilder(64);
        Offset<AuthSchema.LoginResponse> loginResponse =
            AuthSchema.LoginResponse.CreateLoginResponse(
                loginResponseBuilder,
                AuthSchema.LoginResult.Success);
        Offset<AuthSchema.AuthMessage> loginResponseMessage =
            AuthSchema.AuthMessage.CreateAuthMessage(
                loginResponseBuilder,
                1,
                AuthSchema.AuthPayload.LoginResponse,
                loginResponse.Value);
        AuthSchema.AuthMessage.FinishAuthMessageBuffer(
            loginResponseBuilder,
            loginResponseMessage);
        byte[] loginResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.AuthLoginResponse,
            loginHeader.RequestId,
            loginResponseBuilder.SizedByteArray());
        await serverStream.WriteAsync(loginResponseBytes, timeout.Token);

        Assert(await loginTask == AuthLoginResult.Success,
            "Authentication client did not decode the login response.");
    }

}
