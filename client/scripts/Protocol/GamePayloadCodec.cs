using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Text;

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

        return Encoding.ASCII.GetBytes(playerName);
    }

    public static LoginResponseData DecodeLoginResponse(byte[] payload)
    {
        if (payload.Length != 9 || !Enum.IsDefined(typeof(LoginResult), payload[0]))
        {
            throw new InvalidDataException("Invalid login response payload.");
        }

        var result = (LoginResult)payload[0];
        ulong sessionId = ReadUInt64(payload, 1);
        bool succeeded = result == LoginResult.Success;

        if (succeeded != (sessionId != 0))
        {
            throw new InvalidDataException("Invalid login response data.");
        }

        return new LoginResponseData(result, sessionId);
    }

    public static IReadOnlyList<ChannelInfo> DecodeChannelListResponse(
        byte[] payload)
    {
        if (payload.Length < 2)
        {
            throw new InvalidDataException("Invalid channel list payload.");
        }

        ushort channelCount = BinaryPrimitives.ReadUInt16BigEndian(
            payload.AsSpan(0, 2));
        int expectedSize = 2 + channelCount * 12;

        if (payload.Length != expectedSize)
        {
            throw new InvalidDataException("Invalid channel list payload size.");
        }

        var channels = new List<ChannelInfo>(channelCount);
        int offset = 2;

        for (int index = 0; index < channelCount; index++)
        {
            uint id = ReadUInt32(payload, offset);
            uint currentPlayers = ReadUInt32(payload, offset + 4);
            uint maxPlayers = ReadUInt32(payload, offset + 8);
            channels.Add(new ChannelInfo(id, currentPlayers, maxPlayers));
            offset += 12;
        }

        return channels;
    }

    public static byte[] EncodeJoinChannelRequest(uint channelId)
    {
        if (channelId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(channelId));
        }

        var payload = new byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(payload, channelId);
        return payload;
    }

    public static JoinChannelResponse DecodeJoinChannelResponse(byte[] payload)
    {
        if (payload.Length != 5 ||
            !Enum.IsDefined(typeof(JoinChannelResult), payload[0]))
        {
            throw new InvalidDataException("Invalid join channel response payload.");
        }

        var result = (JoinChannelResult)payload[0];
        uint channelId = ReadUInt32(payload, 1);
        return new JoinChannelResponse(result, channelId);
    }

    public static byte[] EncodeCreatePartyRequest()
    {
        return Array.Empty<byte>();
    }

    public static CreatePartyResponse DecodeCreatePartyResponse(byte[] payload)
    {
        if (payload.Length != 17 ||
            !Enum.IsDefined(typeof(CreatePartyResult), payload[0]))
        {
            throw new InvalidDataException("Invalid create party response payload.");
        }

        var result = (CreatePartyResult)payload[0];
        ulong partyId = ReadUInt64(payload, 1);
        ulong leaderSessionId = ReadUInt64(payload, 9);
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

        var payload = new byte[8];
        BinaryPrimitives.WriteUInt64BigEndian(payload, partyId);
        return payload;
    }

    public static JoinPartyResponse DecodeJoinPartyResponse(byte[] payload)
    {
        if (payload.Length != 17 ||
            !Enum.IsDefined(typeof(JoinPartyResult), payload[0]))
        {
            throw new InvalidDataException("Invalid join party response payload.");
        }

        var result = (JoinPartyResult)payload[0];
        ulong partyId = ReadUInt64(payload, 1);
        ulong leaderSessionId = ReadUInt64(payload, 9);
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
        return Array.Empty<byte>();
    }

    public static LeavePartyResult DecodeLeavePartyResponse(byte[] payload)
    {
        if (payload.Length != 1 ||
            !Enum.IsDefined(typeof(LeavePartyResult), payload[0]))
        {
            throw new InvalidDataException("Invalid leave party response payload.");
        }

        return (LeavePartyResult)payload[0];
    }

    public static byte[] EncodeEnterDungeonRequest(uint dungeonTemplateId)
    {
        if (dungeonTemplateId == 0)
        {
            throw new ArgumentOutOfRangeException(nameof(dungeonTemplateId));
        }

        var payload = new byte[4];
        BinaryPrimitives.WriteUInt32BigEndian(payload, dungeonTemplateId);
        return payload;
    }

    public static EnterDungeonResponse DecodeEnterDungeonResponse(byte[] payload)
    {
        ValidateDungeonPayload(payload);

        var result = (EnterDungeonResult)payload[0];
        if (!Enum.IsDefined(typeof(EnterDungeonResult), result))
        {
            throw new InvalidDataException("Invalid enter dungeon result.");
        }

        ulong dungeonId = ReadUInt64(payload, 1);
        ushort udpPort = BinaryPrimitives.ReadUInt16BigEndian(payload.AsSpan(9, 2));
        ulong udpToken = ReadUInt64(payload, 11);
        ValidateDungeonResult(result == EnterDungeonResult.Success,
            dungeonId, udpPort, udpToken);

        return new EnterDungeonResponse(result, dungeonId, udpPort, udpToken);
    }

    public static DungeonConnectionInfo DecodeDungeonConnectionInfoResponse(
        byte[] payload)
    {
        ValidateDungeonPayload(payload);

        var result = (DungeonConnectionInfoResult)payload[0];
        if (!Enum.IsDefined(typeof(DungeonConnectionInfoResult), result))
        {
            throw new InvalidDataException("Invalid dungeon connection result.");
        }

        ulong dungeonId = ReadUInt64(payload, 1);
        ushort udpPort = BinaryPrimitives.ReadUInt16BigEndian(payload.AsSpan(9, 2));
        ulong udpToken = ReadUInt64(payload, 11);
        ValidateDungeonResult(result == DungeonConnectionInfoResult.Success,
            dungeonId, udpPort, udpToken);

        return new DungeonConnectionInfo(result, dungeonId, udpPort, udpToken);
    }

    private static uint ReadUInt32(byte[] bytes, int offset)
    {
        return BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(offset, 4));
    }

    private static ulong ReadUInt64(byte[] bytes, int offset)
    {
        return BinaryPrimitives.ReadUInt64BigEndian(bytes.AsSpan(offset, 8));
    }

    private static void ValidateDungeonPayload(byte[] payload)
    {
        if (payload.Length != 19)
        {
            throw new InvalidDataException("Invalid dungeon response payload.");
        }
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
