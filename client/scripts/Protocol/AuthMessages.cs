using System.Collections.Generic;

namespace DnfMockClient.Protocol;

public enum AuthLoginResult : byte
{
    Success = 0,
    InvalidCredentials = 1,
    ServiceError = 2,
    RateLimited = 3,
    ServiceBusy = 4
}

public enum AuthCharacterListResult : byte
{
    Success = 0,
    NotAuthenticated = 1,
    AccountNotFound = 2,
    ServiceError = 3,
    ServiceBusy = 4
}

public sealed class AuthCharacterSummary
{
    public AuthCharacterSummary(ulong playerId, string displayName, uint level)
    {
        PlayerId = playerId;
        DisplayName = displayName;
        Level = level;
    }

    public ulong PlayerId { get; }
    public string DisplayName { get; }
    public uint Level { get; }
}

public sealed class AuthCharacterListResponse
{
    public AuthCharacterListResponse(
        AuthCharacterListResult result,
        IReadOnlyList<AuthCharacterSummary> characters)
    {
        Result = result;
        Characters = characters;
    }

    public AuthCharacterListResult Result { get; }
    public IReadOnlyList<AuthCharacterSummary> Characters { get; }
}

public enum AuthCharacterSelectionResult : byte
{
    Success = 0,
    NotAuthenticated = 1,
    InvalidSelection = 2,
    ServiceError = 3,
    ServiceBusy = 4
}

public sealed class AuthCharacterSelectionResponse
{
    public AuthCharacterSelectionResponse(
        AuthCharacterSelectionResult result,
        string gameServerHost,
        ushort gameServerPort,
        string authTicket,
        long expiresAtUnix)
    {
        Result = result;
        GameServerHost = gameServerHost;
        GameServerPort = gameServerPort;
        AuthTicket = authTicket;
        ExpiresAtUnix = expiresAtUnix;
    }

    public AuthCharacterSelectionResult Result { get; }
    public string GameServerHost { get; }
    public ushort GameServerPort { get; }
    public string AuthTicket { get; }
    public long ExpiresAtUnix { get; }
}
