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

public sealed class ProtocolCompatibilityTests
{
    [Fact]
    public void TcpSchemaMatchesTheClientContract()
    {
        var builder = new FlatBufferBuilder(128);
        StringOffset authTicket = builder.CreateString("valid-ticket");
        Offset<TcpSchema.LoginRequest> login =
            TcpSchema.LoginRequest.CreateLoginRequest(builder, authTicket);
        Offset<TcpSchema.TcpMessage> message =
            TcpSchema.TcpMessage.CreateTcpMessage(
                builder,
                2,
                TcpSchema.TcpPayload.LoginRequest,
                login.Value);
        TcpSchema.TcpMessage.FinishTcpMessageBuffer(builder, message);

        var buffer = new ByteBuffer(builder.SizedByteArray());
        Assert(TcpSchema.TcpMessage.TcpMessageBufferHasIdentifier(buffer),
            "TCP FlatBuffer identifier is incorrect.");
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(buffer),
            "TCP FlatBuffer verification failed.");

        TcpSchema.TcpMessage decoded =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(buffer);
        Assert(decoded.ProtocolVersion == 2 &&
            decoded.PayloadType == TcpSchema.TcpPayload.LoginRequest &&
            decoded.PayloadAsLoginRequest().AuthTicket == "valid-ticket",
            "TCP FlatBuffer login payload is incorrect.");
    }

    [Fact]
    public void AuthSchemaMatchesTheClientContract()
    {
        var builder = new FlatBufferBuilder(128);
        StringOffset loginId = builder.CreateString("test-user");
        StringOffset password = builder.CreateString("test-password");
        Offset<AuthSchema.LoginRequest> login =
            AuthSchema.LoginRequest.CreateLoginRequest(
                builder,
                loginId,
                password);
        Offset<AuthSchema.AuthMessage> message =
            AuthSchema.AuthMessage.CreateAuthMessage(
                builder,
                1,
                AuthSchema.AuthPayload.LoginRequest,
                login.Value);
        AuthSchema.AuthMessage.FinishAuthMessageBuffer(builder, message);

        var buffer = new ByteBuffer(builder.SizedByteArray());
        Assert(AuthSchema.AuthMessage.AuthMessageBufferHasIdentifier(buffer),
            "Auth FlatBuffer identifier is incorrect.");
        Assert(AuthSchema.AuthMessage.VerifyAuthMessage(buffer),
            "Auth FlatBuffer verification failed.");

        AuthSchema.AuthMessage decoded =
            AuthSchema.AuthMessage.GetRootAsAuthMessage(buffer);
        AuthSchema.LoginRequest decodedLogin = decoded.PayloadAsLoginRequest();
        Assert(decoded.ProtocolVersion == 1 &&
            decoded.PayloadType == AuthSchema.AuthPayload.LoginRequest &&
            decodedLogin.LoginId == "test-user" &&
            decodedLogin.Password == "test-password",
            "Auth FlatBuffer login payload is incorrect.");
    }

}
