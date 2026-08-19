namespace DnfMockClient.Protocol;

public enum TcpPacketType : ushort
{
    LoginRequest = 1,
    ChannelListRequest = 2,
    JoinChannelRequest = 3,
    EnterDungeonRequest = 4,
    DungeonConnectionInfoRequest = 5,
    CreatePartyRequest = 6,
    JoinPartyRequest = 7,

    LoginResponse = 101,
    ChannelListResponse = 102,
    JoinChannelResponse = 103,
    EnterDungeonResponse = 104,
    DungeonConnectionInfoResponse = 105,
    CreatePartyResponse = 106,
    JoinPartyResponse = 107
}

public sealed class TcpPacketHeader
{
    public TcpPacketHeader(
        ushort packetSize,
        TcpPacketType type,
        uint requestId)
    {
        PacketSize = packetSize;
        Type = type;
        RequestId = requestId;
    }

    public ushort PacketSize { get; }
    public TcpPacketType Type { get; }
    public uint RequestId { get; }
}

public sealed class TcpPacket
{
    public TcpPacket(TcpPacketHeader header, byte[] payload)
    {
        Header = header;
        Payload = payload;
    }

    public TcpPacketHeader Header { get; }
    public byte[] Payload { get; }
}
