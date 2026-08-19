using System;
using System.Buffers.Binary;
using System.IO;

namespace DnfMockClient.Protocol;

public static class TcpPacketCodec
{
    public const int HeaderSize = 8;
    public const int MaxPacketSize = 16 * 1024;

    public static byte[] EncodeHeader(TcpPacketHeader header)
    {
        ValidateHeader(header.PacketSize, header.Type);

        var bytes = new byte[HeaderSize];

        BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(0, 2), header.PacketSize);
        BinaryPrimitives.WriteUInt16BigEndian(bytes.AsSpan(2, 2), (ushort)header.Type);
        BinaryPrimitives.WriteUInt32BigEndian(bytes.AsSpan(4, 4), header.RequestId);

        return bytes;
    }

    public static TcpPacketHeader DecodeHeader(byte[] bytes)
    {
        if (bytes.Length != HeaderSize)
        {
            throw new ArgumentException("TCP header must be exactly 8 bytes.", nameof(bytes));
        }

        ushort packetSize = BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(0, 2));
        ushort typeValue = BinaryPrimitives.ReadUInt16BigEndian(bytes.AsSpan(2, 2));
        uint requestId = BinaryPrimitives.ReadUInt32BigEndian(bytes.AsSpan(4, 4));
        var type = (TcpPacketType)typeValue;

        ValidateHeader(packetSize, type);
        return new TcpPacketHeader(packetSize, type, requestId);
    }

    public static byte[] EncodePacket(
        TcpPacketType type,
        uint requestId,
        byte[] payload)
    {
        int packetSize = HeaderSize + payload.Length;
        if (packetSize > MaxPacketSize)
        {
            throw new InvalidDataException("TCP packet is too large.");
        }

        var header = new TcpPacketHeader((ushort)packetSize, type, requestId);
        byte[] headerBytes = EncodeHeader(header);
        var packetBytes = new byte[packetSize];

        Array.Copy(headerBytes, 0, packetBytes, 0, HeaderSize);
        Array.Copy(payload, 0, packetBytes, HeaderSize, payload.Length);

        return packetBytes;
    }

    private static void ValidateHeader(
        ushort packetSize,
        TcpPacketType type)
    {
        if (packetSize < HeaderSize || packetSize > MaxPacketSize)
        {
            throw new InvalidDataException("Invalid TCP packet size.");
        }

        if (!Enum.IsDefined(typeof(TcpPacketType), type))
        {
            throw new InvalidDataException("Invalid TCP packet type.");
        }
    }
}
