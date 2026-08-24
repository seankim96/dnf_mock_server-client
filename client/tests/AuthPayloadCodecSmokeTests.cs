using System.IO;
using DnfMockClient.Protocol;
using Google.FlatBuffers;
using AuthSchema = Dnf.Protocol.Auth;

internal static class AuthPayloadCodecSmokeTests
{
    public static void Run()
    {
        TestLoginPayloads();
        TestCharacterListPayloads();
        TestCharacterSelectionPayloads();
    }

    private static void TestLoginPayloads()
    {
        byte[] requestBytes =
            AuthPayloadCodec.EncodeLoginRequest("account_1", "password");
        AuthSchema.AuthMessage requestMessage = DecodeMessage(requestBytes);
        AuthSchema.LoginRequest request =
            requestMessage.PayloadAsLoginRequest();
        Assert(requestMessage.PayloadType == AuthSchema.AuthPayload.LoginRequest &&
            request.LoginId == "account_1" && request.Password == "password",
            "Auth login request is incorrect.");

        AssertThrows<ArgumentException>(
            () => AuthPayloadCodec.EncodeLoginRequest("abc", "password"),
            "A short login ID was accepted.");
        AssertThrows<ArgumentException>(
            () => AuthPayloadCodec.EncodeLoginRequest("account-1", "password"),
            "An invalid login ID character was accepted.");
        AssertThrows<ArgumentException>(
            () => AuthPayloadCodec.EncodeLoginRequest("account_1", ""),
            "An empty password was accepted.");

        AuthLoginResult response = AuthPayloadCodec.DecodeLoginResponse(
            LoginResponseBytes(AuthSchema.LoginResult.Success));
        Assert(response == AuthLoginResult.Success,
            "Auth login response is incorrect.");
        AssertThrows<InvalidDataException>(
            () => AuthPayloadCodec.DecodeLoginResponse(requestBytes),
            "An auth login request was accepted as a response.");
        AssertThrows<InvalidDataException>(
            () => AuthPayloadCodec.DecodeLoginResponse(
                LoginResponseBytes((AuthSchema.LoginResult)byte.MaxValue)),
            "An unknown auth login result was accepted.");
    }

    private static void TestCharacterListPayloads()
    {
        byte[] requestBytes = AuthPayloadCodec.EncodeCharacterListRequest();
        Assert(DecodeMessage(requestBytes).PayloadType ==
            AuthSchema.AuthPayload.CharacterListRequest,
            "Character list request type is incorrect.");

        AuthCharacterListResponse response =
            AuthPayloadCodec.DecodeCharacterListResponse(
                CharacterListResponseBytes(
                    AuthSchema.CharacterListResult.Success,
                    new[]
                    {
                        (1UL, "PlayerOne", 10U),
                        (2UL, "PlayerTwo", 20U)
                    }));
        Assert(response.Result == AuthCharacterListResult.Success &&
            response.Characters.Count == 2 &&
            response.Characters[1].PlayerId == 2 &&
            response.Characters[1].DisplayName == "PlayerTwo" &&
            response.Characters[1].Level == 20,
            "Character list response is incorrect.");

        AssertThrows<InvalidDataException>(
            () => AuthPayloadCodec.DecodeCharacterListResponse(
                CharacterListResponseBytes(
                    AuthSchema.CharacterListResult.NotAuthenticated,
                    new[] { (1UL, "PlayerOne", 10U) })),
            "A failed character list containing data was accepted.");
    }

    private static void TestCharacterSelectionPayloads()
    {
        byte[] requestBytes =
            AuthPayloadCodec.EncodeCharacterSelectionRequest(7);
        AuthSchema.AuthMessage requestMessage = DecodeMessage(requestBytes);
        Assert(requestMessage.PayloadType ==
            AuthSchema.AuthPayload.CharacterSelectionRequest &&
            requestMessage.PayloadAsCharacterSelectionRequest().PlayerId == 7,
            "Character selection request is incorrect.");
        AssertThrows<ArgumentOutOfRangeException>(
            () => AuthPayloadCodec.EncodeCharacterSelectionRequest(0),
            "A zero player ID was accepted.");

        AuthCharacterSelectionResponse success =
            AuthPayloadCodec.DecodeCharacterSelectionResponse(
                CharacterSelectionResponseBytes(
                    AuthSchema.CharacterSelectionResult.Success,
                    "127.0.0.1",
                    7777,
                    "one-time-ticket",
                    2_000_000_000));
        Assert(success.Result == AuthCharacterSelectionResult.Success &&
            success.GameServerHost == "127.0.0.1" &&
            success.GameServerPort == 7777 &&
            success.AuthTicket == "one-time-ticket" &&
            success.ExpiresAtUnix == 2_000_000_000,
            "Character selection response is incorrect.");

        AuthCharacterSelectionResponse failure =
            AuthPayloadCodec.DecodeCharacterSelectionResponse(
                CharacterSelectionResponseBytes(
                    AuthSchema.CharacterSelectionResult.InvalidSelection,
                    "",
                    0,
                    "",
                    0));
        Assert(failure.Result ==
            AuthCharacterSelectionResult.InvalidSelection,
            "Failed character selection response is incorrect.");

        AssertThrows<InvalidDataException>(
            () => AuthPayloadCodec.DecodeCharacterSelectionResponse(
                CharacterSelectionResponseBytes(
                    AuthSchema.CharacterSelectionResult.Success,
                    "",
                    0,
                    "",
                    0)),
            "A successful selection without connection data was accepted.");
    }

    private static AuthSchema.AuthMessage DecodeMessage(byte[] bytes)
    {
        var buffer = new ByteBuffer(bytes);
        Assert(AuthSchema.AuthMessage.VerifyAuthMessage(buffer),
            "Auth FlatBuffer verification failed.");
        return AuthSchema.AuthMessage.GetRootAsAuthMessage(buffer);
    }

    private static byte[] LoginResponseBytes(AuthSchema.LoginResult result)
    {
        var builder = new FlatBufferBuilder(64);
        Offset<AuthSchema.LoginResponse> response =
            AuthSchema.LoginResponse.CreateLoginResponse(builder, result);
        return FinishResponse(
            builder,
            AuthSchema.AuthPayload.LoginResponse,
            response.Value);
    }

    private static byte[] CharacterListResponseBytes(
        AuthSchema.CharacterListResult result,
        IReadOnlyList<(ulong Id, string Name, uint Level)> characters)
    {
        var builder = new FlatBufferBuilder(256);
        var offsets = new Offset<AuthSchema.CharacterSummary>[characters.Count];

        for (int index = 0; index < characters.Count; index++)
        {
            StringOffset name = builder.CreateString(characters[index].Name);
            offsets[index] = AuthSchema.CharacterSummary.CreateCharacterSummary(
                builder,
                characters[index].Id,
                name,
                characters[index].Level);
        }

        VectorOffset vector =
            AuthSchema.CharacterListResponse.CreateCharactersVector(
                builder,
                offsets);
        Offset<AuthSchema.CharacterListResponse> response =
            AuthSchema.CharacterListResponse.CreateCharacterListResponse(
                builder,
                result,
                vector);
        return FinishResponse(
            builder,
            AuthSchema.AuthPayload.CharacterListResponse,
            response.Value);
    }

    private static byte[] CharacterSelectionResponseBytes(
        AuthSchema.CharacterSelectionResult result,
        string host,
        ushort port,
        string ticket,
        long expiresAtUnix)
    {
        var builder = new FlatBufferBuilder(256);
        StringOffset hostOffset = builder.CreateString(host);
        StringOffset ticketOffset = builder.CreateString(ticket);
        Offset<AuthSchema.CharacterSelectionResponse> response =
            AuthSchema.CharacterSelectionResponse
                .CreateCharacterSelectionResponse(
                    builder,
                    result,
                    hostOffset,
                    port,
                    ticketOffset,
                    expiresAtUnix);
        return FinishResponse(
            builder,
            AuthSchema.AuthPayload.CharacterSelectionResponse,
            response.Value);
    }

    private static byte[] FinishResponse(
        FlatBufferBuilder builder,
        AuthSchema.AuthPayload payloadType,
        int payloadOffset)
    {
        Offset<AuthSchema.AuthMessage> message =
            AuthSchema.AuthMessage.CreateAuthMessage(
                builder,
                1,
                payloadType,
                payloadOffset);
        AuthSchema.AuthMessage.FinishAuthMessageBuffer(builder, message);
        return builder.SizedByteArray();
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }

    private static void AssertThrows<TException>(Action action, string message)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }

        throw new InvalidOperationException(message);
    }
}
