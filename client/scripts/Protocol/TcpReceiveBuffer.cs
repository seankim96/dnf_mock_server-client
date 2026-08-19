using System.Collections.Generic;

namespace DnfMockClient.Protocol;

public sealed class TcpReceiveBuffer
{
    private readonly List<byte> _buffer = new();

    public int Size => _buffer.Count;

    public void Append(byte[] data)
    {
        _buffer.AddRange(data);
    }

    public void Clear()
    {
        _buffer.Clear();
    }

    public bool TryPop(out TcpPacket? packet)
    {
        packet = null;

        if (_buffer.Count < TcpPacketCodec.HeaderSize)
        {
            return false;
        }

        byte[] headerBytes = _buffer
            .GetRange(0, TcpPacketCodec.HeaderSize)
            .ToArray();
        TcpPacketHeader header = TcpPacketCodec.DecodeHeader(headerBytes);

        if (_buffer.Count < header.PacketSize)
        {
            return false;
        }

        int payloadSize = header.PacketSize - TcpPacketCodec.HeaderSize;
        byte[] payload = _buffer
            .GetRange(TcpPacketCodec.HeaderSize, payloadSize)
            .ToArray();

        _buffer.RemoveRange(0, header.PacketSize);
        packet = new TcpPacket(header, payload);
        return true;
    }
}
