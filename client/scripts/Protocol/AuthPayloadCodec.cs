using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Google.FlatBuffers;
using AuthSchema = Dnf.Protocol.Auth;

namespace DnfMockClient.Protocol;

public static class AuthPayloadCodec
{
    private const ushort ProtocolVersion = 1;
    private const int MinLoginIdLength = 4;
    private const int MaxLoginIdLength = 32;
    private const int MaxPasswordLength = 1024;
    private const int MaxAuthTicketLength = 256;

    public static byte[] EncodeLoginRequest(string loginId, string password)
    {
        if (!IsValidLoginId(loginId))
        {
            throw new ArgumentException("Login ID is invalid.", nameof(loginId));
        }

        if (string.IsNullOrEmpty(password) ||
            Encoding.UTF8.GetByteCount(password) > MaxPasswordLength)
        {
            throw new ArgumentException(
                "Password must be 1 to 1024 bytes.",
                nameof(password));
        }

        var builder = new FlatBufferBuilder(256);
        StringOffset loginIdOffset = builder.CreateString(loginId);
        StringOffset passwordOffset = builder.CreateString(password);
        Offset<AuthSchema.LoginRequest> request =
            AuthSchema.LoginRequest.CreateLoginRequest(
                builder,
                loginIdOffset,
                passwordOffset);
        return FinishPayload(
            builder,
            AuthSchema.AuthPayload.LoginRequest,
            request.Value);
    }

    public static AuthLoginResult DecodeLoginResponse(byte[] payload)
    {
        AuthSchema.LoginResponse response =
            DecodePayload<AuthSchema.LoginResponse>(
                payload,
                AuthSchema.AuthPayload.LoginResponse);
        if (!Enum.IsDefined(typeof(AuthSchema.LoginResult), response.Result))
        {
            throw new InvalidDataException("Invalid auth login result.");
        }

        return (AuthLoginResult)(byte)response.Result;
    }

    public static byte[] EncodeCharacterListRequest()
    {
        var builder = new FlatBufferBuilder(64);
        AuthSchema.CharacterListRequest.StartCharacterListRequest(builder);
        Offset<AuthSchema.CharacterListRequest> request =
            AuthSchema.CharacterListRequest.EndCharacterListRequest(builder);
        return FinishPayload(
            builder,
            AuthSchema.AuthPayload.CharacterListRequest,
            request.Value);
    }

    public static AuthCharacterListResponse DecodeCharacterListResponse(
        byte[] payload)
    {
        AuthSchema.CharacterListResponse response =
            DecodePayload<AuthSchema.CharacterListResponse>(
                payload,
                AuthSchema.AuthPayload.CharacterListResponse);
        if (!Enum.IsDefined(
                typeof(AuthSchema.CharacterListResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid character list result.");
        }

        var characters =
            new List<AuthCharacterSummary>(response.CharactersLength);
        var playerIds = new HashSet<ulong>();

        for (int index = 0; index < response.CharactersLength; index++)
        {
            AuthSchema.CharacterSummary? source = response.Characters(index);
            if (!source.HasValue ||
                source.Value.PlayerId == 0 ||
                !playerIds.Add(source.Value.PlayerId) ||
                string.IsNullOrEmpty(source.Value.DisplayName) ||
                source.Value.Level == 0)
            {
                throw new InvalidDataException(
                    "Invalid character list response data.");
            }

            characters.Add(new AuthCharacterSummary(
                source.Value.PlayerId,
                source.Value.DisplayName,
                source.Value.Level));
        }

        var result = (AuthCharacterListResult)(byte)response.Result;
        if (result != AuthCharacterListResult.Success && characters.Count != 0)
        {
            throw new InvalidDataException(
                "Failed character list response contains characters.");
        }

        return new AuthCharacterListResponse(result, characters);
    }

    public static byte[] EncodeCharacterSelectionRequest(ulong playerId)
    {
        if (playerId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(playerId));
        }

        var builder = new FlatBufferBuilder(64);
        Offset<AuthSchema.CharacterSelectionRequest> request =
            AuthSchema.CharacterSelectionRequest
                .CreateCharacterSelectionRequest(builder, playerId);
        return FinishPayload(
            builder,
            AuthSchema.AuthPayload.CharacterSelectionRequest,
            request.Value);
    }

    public static AuthCharacterSelectionResponse
        DecodeCharacterSelectionResponse(byte[] payload)
    {
        AuthSchema.CharacterSelectionResponse response =
            DecodePayload<AuthSchema.CharacterSelectionResponse>(
                payload,
                AuthSchema.AuthPayload.CharacterSelectionResponse);
        if (!Enum.IsDefined(
                typeof(AuthSchema.CharacterSelectionResult),
                response.Result))
        {
            throw new InvalidDataException(
                "Invalid character selection result.");
        }

        var result = (AuthCharacterSelectionResult)(byte)response.Result;
        string host = response.GameServerHost ?? string.Empty;
        string ticket = response.AuthTicket ?? string.Empty;
        bool succeeded = result == AuthCharacterSelectionResult.Success;
        bool hasConnectionData =
            !string.IsNullOrEmpty(host) &&
            response.GameServerPort != 0 &&
            Encoding.UTF8.GetByteCount(ticket) is > 0 and <= MaxAuthTicketLength &&
            response.ExpiresAtUnix > 0;
        bool hasNoConnectionData =
            string.IsNullOrEmpty(host) &&
            response.GameServerPort == 0 &&
            string.IsNullOrEmpty(ticket) &&
            response.ExpiresAtUnix == 0;

        if ((succeeded && !hasConnectionData) ||
            (!succeeded && !hasNoConnectionData))
        {
            throw new InvalidDataException(
                "Invalid character selection response data.");
        }

        return new AuthCharacterSelectionResponse(
            result,
            host,
            response.GameServerPort,
            ticket,
            response.ExpiresAtUnix);
    }

    private static bool IsValidLoginId(string loginId)
    {
        if (string.IsNullOrEmpty(loginId) ||
            loginId.Length < MinLoginIdLength ||
            loginId.Length > MaxLoginIdLength)
        {
            return false;
        }

        foreach (char character in loginId)
        {
            bool valid =
                character is >= 'a' and <= 'z' or
                    >= 'A' and <= 'Z' or
                    >= '0' and <= '9' or '_';
            if (!valid)
            {
                return false;
            }
        }

        return true;
    }

    private static byte[] FinishPayload(
        FlatBufferBuilder builder,
        AuthSchema.AuthPayload payloadType,
        int payloadOffset)
    {
        if (payloadType == AuthSchema.AuthPayload.NONE || payloadOffset == 0)
        {
            throw new ArgumentException("Auth payload must not be empty.");
        }

        Offset<AuthSchema.AuthMessage> message =
            AuthSchema.AuthMessage.CreateAuthMessage(
                builder,
                ProtocolVersion,
                payloadType,
                payloadOffset);
        AuthSchema.AuthMessage.FinishAuthMessageBuffer(builder, message);
        return builder.SizedByteArray();
    }

    private static TPayload DecodePayload<TPayload>(
        byte[] bytes,
        AuthSchema.AuthPayload expectedType)
        where TPayload : struct, IFlatbufferObject
    {
        if (bytes is null || bytes.Length == 0)
        {
            throw new InvalidDataException("Auth FlatBuffer payload is empty.");
        }

        var buffer = new ByteBuffer(bytes);
        if (!AuthSchema.AuthMessage.AuthMessageBufferHasIdentifier(buffer) ||
            !AuthSchema.AuthMessage.VerifyAuthMessage(buffer))
        {
            throw new InvalidDataException("Invalid Auth FlatBuffer payload.");
        }

        AuthSchema.AuthMessage message =
            AuthSchema.AuthMessage.GetRootAsAuthMessage(buffer);
        if (message.ProtocolVersion != ProtocolVersion)
        {
            throw new InvalidDataException(
                "Unsupported Auth payload protocol version.");
        }

        if (message.PayloadType != expectedType)
        {
            throw new InvalidDataException(
                "Unexpected Auth FlatBuffer payload type.");
        }

        TPayload? payload = message.Payload<TPayload>();
        if (!payload.HasValue)
        {
            throw new InvalidDataException("Auth FlatBuffer payload is missing.");
        }

        return payload.Value;
    }
}
