using System.Collections.Generic;

namespace DnfMockClient.Protocol;

public enum LoginResult : byte
{
    Success = 0,
    InvalidTicket = 1,
    PlayerNotFound = 2,
    StorageError = 3
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
    public ChannelInfo(
        uint id,
        string displayName,
        uint currentPlayers,
        uint maxPlayers)
    {
        Id = id;
        DisplayName = displayName;
        CurrentPlayers = currentPlayers;
        MaxPlayers = maxPlayers;
    }

    public uint Id { get; }
    public string DisplayName { get; }
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

public enum JoinPartyResult : byte
{
    Success = 0,
    PartyNotFound = 1,
    PartyFull = 2,
    AlreadyJoined = 3
}

public sealed class JoinPartyResponse
{
    public JoinPartyResponse(
        JoinPartyResult result,
        ulong partyId,
        ulong leaderSessionId)
    {
        Result = result;
        PartyId = partyId;
        LeaderSessionId = leaderSessionId;
    }

    public JoinPartyResult Result { get; }
    public ulong PartyId { get; }
    public ulong LeaderSessionId { get; }
}

public enum LeavePartyResult : byte
{
    Success = 0,
    NotInParty = 1
}

public enum PartySnapshotResult : byte
{
    Success = 0,
    NotInParty = 1
}

public sealed class PartySnapshotData
{
    public PartySnapshotData(
        PartySnapshotResult result,
        ulong partyId,
        ulong leaderSessionId,
        IReadOnlyList<ulong> members)
    {
        Result = result;
        PartyId = partyId;
        LeaderSessionId = leaderSessionId;
        Members = members;
    }

    public PartySnapshotResult Result { get; }
    public ulong PartyId { get; }
    public ulong LeaderSessionId { get; }
    public IReadOnlyList<ulong> Members { get; }
}

public enum CatalogResult : byte
{
    Success = 0,
    Unavailable = 1
}

public sealed class DungeonCatalogEntry
{
    public DungeonCatalogEntry(
        uint templateId,
        string displayName,
        byte recommendedPartySize,
        byte maxPartySize,
        bool available)
    {
        TemplateId = templateId;
        DisplayName = displayName;
        RecommendedPartySize = recommendedPartySize;
        MaxPartySize = maxPartySize;
        Available = available;
    }

    public uint TemplateId { get; }
    public string DisplayName { get; }
    public byte RecommendedPartySize { get; }
    public byte MaxPartySize { get; }
    public bool Available { get; }
}

public sealed class DungeonCatalogData
{
    public DungeonCatalogData(
        CatalogResult result,
        IReadOnlyList<DungeonCatalogEntry> dungeons)
    {
        Result = result;
        Dungeons = dungeons;
    }

    public CatalogResult Result { get; }
    public IReadOnlyList<DungeonCatalogEntry> Dungeons { get; }
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
