using System;
using System.IO;
using Google.FlatBuffers;
using TcpSchema = Dnf.Protocol.Tcp;

namespace DnfMockClient.Protocol;

public static class TcpFlatBufferCodec
{
    public const ushort ProtocolVersion = 2;

    public static byte[] FinishPayload(
        FlatBufferBuilder builder,
        TcpSchema.TcpPayload payloadType,
        int payloadOffset)
    {
        if (payloadType == TcpSchema.TcpPayload.NONE || payloadOffset == 0)
        {
            throw new ArgumentException("TCP payload must not be empty.");
        }

        Offset<TcpSchema.TcpMessage> message =
            TcpSchema.TcpMessage.CreateTcpMessage(
                builder,
                ProtocolVersion,
                payloadType,
                payloadOffset);
        TcpSchema.TcpMessage.FinishTcpMessageBuffer(builder, message);
        return builder.SizedByteArray();
    }

    public static TPayload DecodePayload<TPayload>(
        byte[] bytes,
        TcpSchema.TcpPayload expectedType)
        where TPayload : struct, IFlatbufferObject
    {
        if (bytes.Length == 0)
        {
            throw new InvalidDataException("TCP FlatBuffer payload is empty.");
        }

        var buffer = new ByteBuffer(bytes);
        if (!TcpSchema.TcpMessage.TcpMessageBufferHasIdentifier(buffer) ||
            !TcpSchema.TcpMessage.VerifyTcpMessage(buffer))
        {
            throw new InvalidDataException("Invalid TCP FlatBuffer payload.");
        }

        TcpSchema.TcpMessage message =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(buffer);
        if (message.ProtocolVersion != ProtocolVersion)
        {
            throw new InvalidDataException(
                "Unsupported TCP payload protocol version.");
        }

        if (message.PayloadType != expectedType)
        {
            throw new InvalidDataException(
                "Unexpected TCP FlatBuffer payload type.");
        }

        TPayload? payload = message.Payload<TPayload>();
        if (!payload.HasValue)
        {
            throw new InvalidDataException("TCP FlatBuffer payload is missing.");
        }

        return payload.Value;
    }
}
