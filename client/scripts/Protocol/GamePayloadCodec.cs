using System;
using System.Collections.Generic;
using System.IO;
using Google.FlatBuffers;
using TcpSchema = Dnf.Protocol.Tcp;

namespace DnfMockClient.Protocol;

public static class GamePayloadCodec
{
    public static byte[] EncodeLoginRequest(string playerName)
    {
        if (string.IsNullOrEmpty(playerName) || playerName.Length > 16)
        {
            throw new ArgumentException("Player name must be 1 to 16 characters.");
        }

        foreach (char character in playerName)
        {
            bool allowed = char.IsAsciiLetterOrDigit(character) || character == '_';
            if (!allowed)
            {
                throw new ArgumentException(
                    "Player name may contain only letters, numbers, and underscore.");
            }
        }

        var builder = new FlatBufferBuilder(128);
        StringOffset name = builder.CreateString(playerName);
        Offset<TcpSchema.LoginRequest> request =
            TcpSchema.LoginRequest.CreateLoginRequest(builder, name);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.LoginRequest,
            request.Value);
    }

    public static LoginResponseData DecodeLoginResponse(byte[] payload)
    {
        TcpSchema.LoginResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.LoginResponse>(
                payload,
                TcpSchema.TcpPayload.LoginResponse);
        if (!Enum.IsDefined(typeof(TcpSchema.LoginResult), response.Result))
        {
            throw new InvalidDataException("Invalid login response payload.");
        }

        var result = (LoginResult)(byte)response.Result;
        ulong sessionId = response.SessionId;
        bool succeeded = result == LoginResult.Success;

        if (succeeded != (sessionId != 0))
        {
            throw new InvalidDataException("Invalid login response data.");
        }

        return new LoginResponseData(result, sessionId);
    }

    public static byte[] EncodeChannelListRequest()
    {
        var builder = new FlatBufferBuilder(64);
        TcpSchema.ChannelListRequest.StartChannelListRequest(builder);
        Offset<TcpSchema.ChannelListRequest> request =
            TcpSchema.ChannelListRequest.EndChannelListRequest(builder);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.ChannelListRequest,
            request.Value);
    }

    public static IReadOnlyList<ChannelInfo> DecodeChannelListResponse(
        byte[] payload)
    {
        TcpSchema.ChannelListResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.ChannelListResponse>(
                payload,
                TcpSchema.TcpPayload.ChannelListResponse);

        var channels = new List<ChannelInfo>(response.ChannelsLength);

        for (int index = 0; index < response.ChannelsLength; index++)
        {
            TcpSchema.ChannelInfo? source = response.Channels(index);
            if (!source.HasValue || source.Value.ChannelId == 0 ||
                string.IsNullOrEmpty(source.Value.DisplayName) ||
                source.Value.MaxPlayers == 0 ||
                source.Value.CurrentPlayers > source.Value.MaxPlayers)
            {
                throw new InvalidDataException("Invalid channel list response data.");
            }

            channels.Add(new ChannelInfo(
                source.Value.ChannelId,
                source.Value.DisplayName,
                source.Value.CurrentPlayers,
                source.Value.MaxPlayers));
        }

        return channels;
    }

    public static byte[] EncodeJoinChannelRequest(uint channelId)
    {
        if (channelId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(channelId));
        }

        var builder = new FlatBufferBuilder(64);
        Offset<TcpSchema.JoinChannelRequest> request =
            TcpSchema.JoinChannelRequest.CreateJoinChannelRequest(
                builder,
                channelId);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.JoinChannelRequest,
            request.Value);
    }

    public static JoinChannelResponse DecodeJoinChannelResponse(byte[] payload)
    {
        TcpSchema.JoinChannelResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.JoinChannelResponse>(
                payload,
                TcpSchema.TcpPayload.JoinChannelResponse);
        if (!Enum.IsDefined(
                typeof(TcpSchema.JoinChannelResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid join channel response payload.");
        }

        var result = (JoinChannelResult)(byte)response.Result;
        uint channelId = response.ChannelId;
        bool succeeded = result == JoinChannelResult.Success;
        if (succeeded != (channelId != 0))
        {
            throw new InvalidDataException("Invalid join channel response data.");
        }

        return new JoinChannelResponse(result, channelId);
    }

    public static byte[] EncodeCreatePartyRequest()
    {
        var builder = new FlatBufferBuilder(64);
        TcpSchema.CreatePartyRequest.StartCreatePartyRequest(builder);
        Offset<TcpSchema.CreatePartyRequest> request =
            TcpSchema.CreatePartyRequest.EndCreatePartyRequest(builder);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.CreatePartyRequest,
            request.Value);
    }

    public static CreatePartyResponse DecodeCreatePartyResponse(byte[] payload)
    {
        TcpSchema.CreatePartyResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.CreatePartyResponse>(
                payload,
                TcpSchema.TcpPayload.CreatePartyResponse);
        if (!Enum.IsDefined(
                typeof(TcpSchema.CreatePartyResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid create party response payload.");
        }

        var result = (CreatePartyResult)(byte)response.Result;
        ulong partyId = response.PartyId;
        ulong leaderSessionId = response.LeaderSessionId;
        bool succeeded = result == CreatePartyResult.Success;
        bool hasPartyData = partyId != 0 && leaderSessionId != 0;
        bool hasNoPartyData = partyId == 0 && leaderSessionId == 0;

        if ((succeeded && !hasPartyData) || (!succeeded && !hasNoPartyData))
        {
            throw new InvalidDataException("Invalid create party response data.");
        }

        return new CreatePartyResponse(result, partyId, leaderSessionId);
    }

    public static byte[] EncodeJoinPartyRequest(ulong partyId)
    {
        if (partyId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(partyId));
        }

        var builder = new FlatBufferBuilder(64);
        Offset<TcpSchema.JoinPartyRequest> request =
            TcpSchema.JoinPartyRequest.CreateJoinPartyRequest(
                builder,
                partyId);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.JoinPartyRequest,
            request.Value);
    }

    public static JoinPartyResponse DecodeJoinPartyResponse(byte[] payload)
    {
        TcpSchema.JoinPartyResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.JoinPartyResponse>(
                payload,
                TcpSchema.TcpPayload.JoinPartyResponse);
        if (!Enum.IsDefined(
                typeof(TcpSchema.JoinPartyResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid join party response payload.");
        }

        var result = (JoinPartyResult)(byte)response.Result;
        ulong partyId = response.PartyId;
        ulong leaderSessionId = response.LeaderSessionId;
        bool succeeded = result == JoinPartyResult.Success;
        bool hasPartyData = partyId != 0 && leaderSessionId != 0;
        bool hasNoPartyData = partyId == 0 && leaderSessionId == 0;

        if ((succeeded && !hasPartyData) || (!succeeded && !hasNoPartyData))
        {
            throw new InvalidDataException("Invalid join party response data.");
        }

        return new JoinPartyResponse(result, partyId, leaderSessionId);
    }

    public static byte[] EncodeLeavePartyRequest()
    {
        var builder = new FlatBufferBuilder(64);
        TcpSchema.LeavePartyRequest.StartLeavePartyRequest(builder);
        Offset<TcpSchema.LeavePartyRequest> request =
            TcpSchema.LeavePartyRequest.EndLeavePartyRequest(builder);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.LeavePartyRequest,
            request.Value);
    }

    public static LeavePartyResult DecodeLeavePartyResponse(byte[] payload)
    {
        TcpSchema.LeavePartyResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.LeavePartyResponse>(
                payload,
                TcpSchema.TcpPayload.LeavePartyResponse);
        if (!Enum.IsDefined(
                typeof(TcpSchema.LeavePartyResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid leave party response payload.");
        }

        return (LeavePartyResult)(byte)response.Result;
    }

    public static byte[] EncodePartySnapshotRequest()
    {
        var builder = new FlatBufferBuilder(64);
        TcpSchema.PartySnapshotRequest.StartPartySnapshotRequest(builder);
        Offset<TcpSchema.PartySnapshotRequest> request =
            TcpSchema.PartySnapshotRequest.EndPartySnapshotRequest(builder);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.PartySnapshotRequest,
            request.Value);
    }

    public static PartySnapshotData DecodePartySnapshotResponse(byte[] payload)
    {
        TcpSchema.PartySnapshotResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.PartySnapshotResponse>(
                payload,
                TcpSchema.TcpPayload.PartySnapshotResponse);
        if (!Enum.IsDefined(
                typeof(TcpSchema.PartySnapshotResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid party snapshot response payload.");
        }

        var result = (PartySnapshotResult)(byte)response.Result;
        ulong partyId = response.PartyId;
        ulong leaderSessionId = response.LeaderSessionId;
        int memberCount = response.MemberSessionIdsLength;

        var members = new List<ulong>(memberCount);
        var uniqueMembers = new HashSet<ulong>();
        bool containsLeader = false;

        for (int index = 0; index < memberCount; index++)
        {
            ulong memberSessionId = response.MemberSessionIds(index);
            if (memberSessionId == 0 || !uniqueMembers.Add(memberSessionId))
            {
                throw new InvalidDataException("Invalid party member data.");
            }

            containsLeader |= memberSessionId == leaderSessionId;
            members.Add(memberSessionId);
        }

        if (result == PartySnapshotResult.Success)
        {
            if (partyId == 0 || leaderSessionId == 0 ||
                memberCount is < 1 or > 4 || !containsLeader)
            {
                throw new InvalidDataException("Invalid successful party snapshot.");
            }
        }
        else if (partyId != 0 || leaderSessionId != 0 || memberCount != 0)
        {
            throw new InvalidDataException("Invalid failed party snapshot.");
        }

        return new PartySnapshotData(
            result,
            partyId,
            leaderSessionId,
            members);
    }

    public static byte[] EncodeDungeonCatalogRequest()
    {
        var builder = new FlatBufferBuilder(64);
        TcpSchema.DungeonCatalogRequest.StartDungeonCatalogRequest(builder);
        Offset<TcpSchema.DungeonCatalogRequest> request =
            TcpSchema.DungeonCatalogRequest.EndDungeonCatalogRequest(builder);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.DungeonCatalogRequest,
            request.Value);
    }

    public static DungeonCatalogData DecodeDungeonCatalogResponse(
        byte[] payload)
    {
        TcpSchema.DungeonCatalogResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.DungeonCatalogResponse>(
                payload,
                TcpSchema.TcpPayload.DungeonCatalogResponse);
        if (!Enum.IsDefined(typeof(TcpSchema.CatalogResult), response.Result))
        {
            throw new InvalidDataException("Invalid dungeon catalog result.");
        }

        var result = (CatalogResult)(byte)response.Result;
        if (result == CatalogResult.Unavailable && response.DungeonsLength != 0)
        {
            throw new InvalidDataException("Unavailable catalog must be empty.");
        }

        var dungeons = new List<DungeonCatalogEntry>(response.DungeonsLength);
        var templateIds = new HashSet<uint>();

        for (int index = 0; index < response.DungeonsLength; index++)
        {
            TcpSchema.DungeonCatalogEntry? source = response.Dungeons(index);
            if (!source.HasValue || source.Value.DungeonTemplateId == 0 ||
                string.IsNullOrEmpty(source.Value.DisplayName) ||
                source.Value.RecommendedPartySize == 0 ||
                source.Value.MaxPartySize == 0 ||
                source.Value.RecommendedPartySize > source.Value.MaxPartySize ||
                source.Value.MaxPartySize > 4 ||
                !templateIds.Add(source.Value.DungeonTemplateId))
            {
                throw new InvalidDataException("Invalid dungeon catalog entry.");
            }

            dungeons.Add(new DungeonCatalogEntry(
                source.Value.DungeonTemplateId,
                source.Value.DisplayName,
                source.Value.RecommendedPartySize,
                source.Value.MaxPartySize,
                source.Value.Available));
        }

        return new DungeonCatalogData(result, dungeons);
    }

    public static byte[] EncodeEnterDungeonRequest(uint dungeonTemplateId)
    {
        if (dungeonTemplateId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(dungeonTemplateId));
        }

        var builder = new FlatBufferBuilder(64);
        Offset<TcpSchema.EnterDungeonRequest> request =
            TcpSchema.EnterDungeonRequest.CreateEnterDungeonRequest(
                builder,
                dungeonTemplateId);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.EnterDungeonRequest,
            request.Value);
    }

    public static EnterDungeonResponse DecodeEnterDungeonResponse(byte[] payload)
    {
        TcpSchema.EnterDungeonResponse response =
            TcpFlatBufferCodec.DecodePayload<TcpSchema.EnterDungeonResponse>(
                payload,
                TcpSchema.TcpPayload.EnterDungeonResponse);
        if (!Enum.IsDefined(
                typeof(TcpSchema.EnterDungeonResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid enter dungeon result.");
        }

        var result = (EnterDungeonResult)(byte)response.Result;
        ulong dungeonId = response.DungeonId;
        ushort udpPort = response.UdpPort;
        ulong udpToken = response.UdpToken;
        ValidateDungeonResult(result == EnterDungeonResult.Success,
            dungeonId, udpPort, udpToken);

        return new EnterDungeonResponse(result, dungeonId, udpPort, udpToken);
    }

    public static byte[] EncodeDungeonConnectionInfoRequest()
    {
        var builder = new FlatBufferBuilder(64);
        TcpSchema.DungeonConnectionInfoRequest
            .StartDungeonConnectionInfoRequest(builder);
        Offset<TcpSchema.DungeonConnectionInfoRequest> request =
            TcpSchema.DungeonConnectionInfoRequest
                .EndDungeonConnectionInfoRequest(builder);
        return TcpFlatBufferCodec.FinishPayload(
            builder,
            TcpSchema.TcpPayload.DungeonConnectionInfoRequest,
            request.Value);
    }

    public static DungeonConnectionInfo DecodeDungeonConnectionInfoResponse(
        byte[] payload)
    {
        TcpSchema.DungeonConnectionInfoResponse response =
            TcpFlatBufferCodec.DecodePayload<
                TcpSchema.DungeonConnectionInfoResponse>(
                payload,
                TcpSchema.TcpPayload.DungeonConnectionInfoResponse);
        if (!Enum.IsDefined(
                typeof(TcpSchema.DungeonConnectionInfoResult),
                response.Result))
        {
            throw new InvalidDataException("Invalid dungeon connection result.");
        }

        var result = (DungeonConnectionInfoResult)(byte)response.Result;
        ulong dungeonId = response.DungeonId;
        ushort udpPort = response.UdpPort;
        ulong udpToken = response.UdpToken;
        ValidateDungeonResult(result == DungeonConnectionInfoResult.Success,
            dungeonId, udpPort, udpToken);

        return new DungeonConnectionInfo(result, dungeonId, udpPort, udpToken);
    }

    private static void ValidateDungeonResult(
        bool succeeded,
        ulong dungeonId,
        ushort udpPort,
        ulong udpToken)
    {
        bool hasConnectionData = dungeonId != 0 && udpPort != 0 && udpToken != 0;
        bool hasNoConnectionData = dungeonId == 0 && udpPort == 0 && udpToken == 0;

        if ((succeeded && !hasConnectionData) || (!succeeded && !hasNoConnectionData))
        {
            throw new InvalidDataException("Invalid dungeon connection data.");
        }
    }
}
