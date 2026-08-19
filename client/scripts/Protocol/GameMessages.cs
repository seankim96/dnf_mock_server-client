using System.Collections.Generic;

namespace DnfMockClient.Protocol;

public enum LoginResult : byte
{
    Success = 0,
    EmptyPlayerName = 1,
    PlayerNameTooLong = 2,
    InvalidPlayerNameCharacter = 3
}

public sealed class LoginResponseData
{
    public LoginResponseData(LoginResult result, ulong sessionId)
    {
        Result = result;
        SessionId = sessionId;
    }

    public LoginResult Result { get; }
    public ulong SessionId { get; }
}

public sealed class ChannelInfo
{
    public ChannelInfo(uint id, uint currentPlayers, uint maxPlayers)
    {
        Id = id;
        CurrentPlayers = currentPlayers;
        MaxPlayers = maxPlayers;
    }

    public uint Id { get; }
    public uint CurrentPlayers { get; }
    public uint MaxPlayers { get; }
}

public enum JoinChannelResult : byte
{
    Success = 0,
    ChannelNotFound = 1,
    ChannelFull = 2,
    AlreadyJoined = 3
}

public sealed class JoinChannelResponse
{
    public JoinChannelResponse(JoinChannelResult result, uint channelId)
    {
        Result = result;
        ChannelId = channelId;
    }

    public JoinChannelResult Result { get; }
    public uint ChannelId { get; }
}

public enum CreatePartyResult : byte
{
    Success = 0,
    AlreadyInParty = 1
}

public sealed class CreatePartyResponse
{
    public CreatePartyResponse(
        CreatePartyResult result,
        ulong partyId,
        ulong leaderSessionId)
    {
        Result = result;
        PartyId = partyId;
        LeaderSessionId = leaderSessionId;
    }

    public CreatePartyResult Result { get; }
    public ulong PartyId { get; }
    public ulong LeaderSessionId { get; }
}

public enum EnterDungeonResult : byte
{
    Success = 0,
    NotInParty = 1,
    NotPartyLeader = 2,
    DungeonTemplateNotFound = 3,
    PartyAlreadyInDungeon = 4,
    UdpAllocationFailed = 5
}

public sealed class EnterDungeonResponse
{
    public EnterDungeonResponse(
        EnterDungeonResult result,
        ulong dungeonId,
        ushort udpPort,
        ulong udpToken)
    {
        Result = result;
        DungeonId = dungeonId;
        UdpPort = udpPort;
        UdpToken = udpToken;
    }

    public EnterDungeonResult Result { get; }
    public ulong DungeonId { get; }
    public ushort UdpPort { get; }
    public ulong UdpToken { get; }
}

public enum DungeonConnectionInfoResult : byte
{
    Success = 0,
    NotInParty = 1,
    DungeonNotFound = 2,
    NotDungeonParticipant = 3,
    UdpNotReady = 4
}

public sealed class DungeonConnectionInfo
{
    public DungeonConnectionInfo(
        DungeonConnectionInfoResult result,
        ulong dungeonId,
        ushort udpPort,
        ulong udpToken)
    {
        Result = result;
        DungeonId = dungeonId;
        UdpPort = udpPort;
        UdpToken = udpToken;
    }

    public DungeonConnectionInfoResult Result { get; }
    public ulong DungeonId { get; }
    public ushort UdpPort { get; }
    public ulong UdpToken { get; }
}
