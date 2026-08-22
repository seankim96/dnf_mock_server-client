namespace DnfMockClient.Protocol;

public enum UdpHelloResult : byte
{
    Success = 0,
    InvalidDungeon = 1,
    AuthenticationFailed = 2
}

public sealed class UdpHelloAckData
{
    public UdpHelloAckData(
        ulong dungeonId,
        UdpHelloResult result,
        uint serverTick)
    {
        DungeonId = dungeonId;
        Result = result;
        ServerTick = serverTick;
    }

    public ulong DungeonId { get; }
    public UdpHelloResult Result { get; }
    public uint ServerTick { get; }
}
