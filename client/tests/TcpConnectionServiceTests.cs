using System.IO;
using System.Net;
using System.Net.Security;
using System.Net.Sockets;
using System.Security.Authentication;
using System.Security.Cryptography;
using System.Security.Cryptography.X509Certificates;
using Dnf.Protocol;
using DnfMockClient.Networking;
using DnfMockClient.Protocol;
using Google.FlatBuffers;
using Xunit;
using static DnfMockClient.Tests.TestAssertions;
using static DnfMockClient.Tests.TestPayloads;
using AuthSchema = Dnf.Protocol.Auth;
using TcpSchema = Dnf.Protocol.Tcp;

namespace DnfMockClient.Tests;

[Collection(SocketTestCollection.Name)]
public sealed class TcpConnectionServiceTests
{
    [Fact]
    public async Task ExchangesCorrelatedGameRequestsOverTcp()
    {
        using var listener = new TcpListener(IPAddress.Loopback, 0);
        listener.Start();

        int port = ((IPEndPoint)listener.LocalEndpoint).Port;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
        Task<TcpClient> acceptTask =
            listener.AcceptTcpClientAsync(timeout.Token).AsTask();

        using var connection = new TcpConnectionService();
        await connection.ConnectAsync("127.0.0.1", port, timeout.Token);
        using TcpClient acceptedClient = await acceptTask;

        if (!connection.IsConnected)
        {
            throw new InvalidOperationException("TCP connection was not established.");
        }

        Task<TcpPacket> responseTask = connection.SendRequestAsync(
            TcpPacketType.LoginRequest,
            GamePayloadCodec.EncodeLoginRequest("valid-ticket"),
            timeout.Token);

        NetworkStream serverStream = acceptedClient.GetStream();
        var requestHeaderBytes = new byte[TcpPacketCodec.HeaderSize];
        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader requestHeader = TcpPacketCodec.DecodeHeader(requestHeaderBytes);

        int requestPayloadSize = requestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var requestPayload = new byte[requestPayloadSize];
        await serverStream.ReadExactlyAsync(requestPayload, timeout.Token);

        Assert(requestHeader.Type == TcpPacketType.LoginRequest,
            "TCP request type is incorrect.");
        var loginRequestBuffer = new ByteBuffer(requestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(loginRequestBuffer),
            "TCP login request FlatBuffer is invalid.");
        TcpSchema.TcpMessage loginRequestMessage =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(loginRequestBuffer);
        Assert(loginRequestMessage.PayloadType == TcpSchema.TcpPayload.LoginRequest &&
            loginRequestMessage.PayloadAsLoginRequest().AuthTicket == "valid-ticket",
            "TCP login request payload is incorrect.");

        byte[] responseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.LoginResponse,
            requestHeader.RequestId,
            LoginResponseBytes(TcpSchema.LoginResult.Success, 42));

        await serverStream.WriteAsync(responseBytes[..3], timeout.Token);
        await serverStream.WriteAsync(responseBytes[3..], timeout.Token);

        TcpPacket response = await responseTask;
        Assert(response.Header.Type == TcpPacketType.LoginResponse,
            "TCP response type is incorrect.");
        LoginResponseData loginResponse =
            GamePayloadCodec.DecodeLoginResponse(response.Payload);
        Assert(loginResponse.Result == LoginResult.Success &&
            loginResponse.SessionId == 42,
            "TCP login response payload is incorrect.");

        Task<TcpPacket> channelListResponseTask = connection.SendRequestAsync(
            TcpPacketType.ChannelListRequest,
            GamePayloadCodec.EncodeChannelListRequest(),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader channelListRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        int channelListRequestPayloadSize =
            channelListRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var channelListRequestPayload = new byte[channelListRequestPayloadSize];
        await serverStream.ReadExactlyAsync(
            channelListRequestPayload,
            timeout.Token);
        Assert(channelListRequestHeader.Type == TcpPacketType.ChannelListRequest,
            "Channel list TCP request type is incorrect.");
        var channelListRequestBuffer = new ByteBuffer(channelListRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(channelListRequestBuffer),
            "Channel list TCP request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(channelListRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.ChannelListRequest,
            "Channel list TCP request FlatBuffer type is incorrect.");

        byte[] channelListResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.ChannelListResponse,
            channelListRequestHeader.RequestId,
            ChannelListResponseBytes(new[]
            {
                (1U, "Channel 1", 2U, 100U)
            }));
        await serverStream.WriteAsync(channelListResponseBytes, timeout.Token);

        TcpPacket channelListResponse = await channelListResponseTask;
        IReadOnlyList<ChannelInfo> receivedChannels =
            GamePayloadCodec.DecodeChannelListResponse(channelListResponse.Payload);
        Assert(receivedChannels.Count == 1 &&
            receivedChannels[0].DisplayName == "Channel 1",
            "Channel list TCP response payload is incorrect.");

        Task<TcpPacket> catalogResponseTask = connection.SendRequestAsync(
            TcpPacketType.DungeonCatalogRequest,
            GamePayloadCodec.EncodeDungeonCatalogRequest(),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader catalogRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        int catalogRequestPayloadSize =
            catalogRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var catalogRequestPayload = new byte[catalogRequestPayloadSize];
        await serverStream.ReadExactlyAsync(catalogRequestPayload, timeout.Token);
        Assert(catalogRequestHeader.Type == TcpPacketType.DungeonCatalogRequest,
            "Dungeon catalog TCP request type is incorrect.");
        var catalogRequestBuffer = new ByteBuffer(catalogRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(catalogRequestBuffer),
            "Dungeon catalog TCP request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(catalogRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.DungeonCatalogRequest,
            "Dungeon catalog TCP request FlatBuffer type is incorrect.");

        byte[] catalogResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.DungeonCatalogResponse,
            catalogRequestHeader.RequestId,
            DungeonCatalogResponseBytes(
                TcpSchema.CatalogResult.Success,
                new[]
                {
                    (1001U, "Forest", (byte)1, (byte)4, true)
                }));
        await serverStream.WriteAsync(catalogResponseBytes, timeout.Token);

        TcpPacket catalogResponse = await catalogResponseTask;
        DungeonCatalogData receivedCatalog =
            GamePayloadCodec.DecodeDungeonCatalogResponse(catalogResponse.Payload);
        Assert(catalogResponse.Header.Type == TcpPacketType.DungeonCatalogResponse &&
            receivedCatalog.Dungeons.Count == 1 &&
            receivedCatalog.Dungeons[0].TemplateId == 1001,
            "Dungeon catalog TCP response is incorrect.");

        Task<TcpPacket> joinChannelResponseTask = connection.SendRequestAsync(
            TcpPacketType.JoinChannelRequest,
            GamePayloadCodec.EncodeJoinChannelRequest(1),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader joinChannelRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        int joinChannelRequestPayloadSize =
            joinChannelRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var joinChannelRequestPayload = new byte[joinChannelRequestPayloadSize];
        await serverStream.ReadExactlyAsync(
            joinChannelRequestPayload,
            timeout.Token);
        Assert(joinChannelRequestHeader.Type == TcpPacketType.JoinChannelRequest,
            "Join channel TCP request type is incorrect.");
        var joinChannelRequestBuffer = new ByteBuffer(joinChannelRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(joinChannelRequestBuffer),
            "Join channel TCP request FlatBuffer is invalid.");
        TcpSchema.TcpMessage joinChannelRequest =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(joinChannelRequestBuffer);
        Assert(joinChannelRequest.PayloadType ==
            TcpSchema.TcpPayload.JoinChannelRequest &&
            joinChannelRequest.PayloadAsJoinChannelRequest().ChannelId == 1,
            "Join channel TCP request payload is incorrect.");

        byte[] joinChannelResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.JoinChannelResponse,
            joinChannelRequestHeader.RequestId,
            JoinChannelResponseBytes(
                TcpSchema.JoinChannelResult.Success,
                1));
        await serverStream.WriteAsync(joinChannelResponseBytes, timeout.Token);

        TcpPacket joinChannelResponse = await joinChannelResponseTask;
        JoinChannelResponse receivedJoinChannel =
            GamePayloadCodec.DecodeJoinChannelResponse(joinChannelResponse.Payload);
        Assert(receivedJoinChannel.Result == JoinChannelResult.Success &&
            receivedJoinChannel.ChannelId == 1,
            "Join channel TCP response payload is incorrect.");

        Task<TcpPacket> partyResponseTask = connection.SendRequestAsync(
            TcpPacketType.CreatePartyRequest,
            GamePayloadCodec.EncodeCreatePartyRequest(),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader partyRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        Assert(partyRequestHeader.Type == TcpPacketType.CreatePartyRequest,
            "Create party TCP request type is incorrect.");
        int partyRequestPayloadSize =
            partyRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var partyRequestPayload = new byte[partyRequestPayloadSize];
        await serverStream.ReadExactlyAsync(partyRequestPayload, timeout.Token);
        var partyRequestBuffer = new ByteBuffer(partyRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(partyRequestBuffer),
            "Create party TCP request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(partyRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.CreatePartyRequest,
            "Create party TCP request FlatBuffer type is incorrect.");

        byte[] partyResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.CreatePartyResponse,
            partyRequestHeader.RequestId,
            CreatePartyResponseBytes(
                TcpSchema.CreatePartyResult.Success,
                9,
                42));
        await serverStream.WriteAsync(partyResponseBytes, timeout.Token);

        TcpPacket partyResponse = await partyResponseTask;
        Assert(partyResponse.Header.Type == TcpPacketType.CreatePartyResponse,
            "Create party TCP response type is incorrect.");

        Task<TcpPacket> joinPartyResponseTask = connection.SendRequestAsync(
            TcpPacketType.JoinPartyRequest,
            GamePayloadCodec.EncodeJoinPartyRequest(9),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader joinPartyRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        int joinPartyRequestPayloadSize =
            joinPartyRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var joinPartyRequestPayload = new byte[joinPartyRequestPayloadSize];
        await serverStream.ReadExactlyAsync(joinPartyRequestPayload, timeout.Token);
        Assert(joinPartyRequestHeader.Type == TcpPacketType.JoinPartyRequest,
            "Join party TCP request type is incorrect.");
        var joinPartyRequestBuffer = new ByteBuffer(joinPartyRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(joinPartyRequestBuffer),
            "Join party TCP request FlatBuffer is invalid.");
        TcpSchema.TcpMessage joinPartyRequest =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(joinPartyRequestBuffer);
        Assert(joinPartyRequest.PayloadType == TcpSchema.TcpPayload.JoinPartyRequest &&
            joinPartyRequest.PayloadAsJoinPartyRequest().PartyId == 9,
            "Join party TCP request FlatBuffer data is incorrect.");

        byte[] joinPartyResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.JoinPartyResponse,
            joinPartyRequestHeader.RequestId,
            JoinPartyResponseBytes(
                TcpSchema.JoinPartyResult.Success,
                9,
                42));
        await serverStream.WriteAsync(joinPartyResponseBytes, timeout.Token);

        TcpPacket joinPartyResponse = await joinPartyResponseTask;
        Assert(joinPartyResponse.Header.Type == TcpPacketType.JoinPartyResponse,
            "Join party TCP response type is incorrect.");

        Task<TcpPacket> snapshotResponseTask = connection.SendRequestAsync(
            TcpPacketType.PartySnapshotRequest,
            GamePayloadCodec.EncodePartySnapshotRequest(),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader snapshotRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        Assert(snapshotRequestHeader.Type == TcpPacketType.PartySnapshotRequest,
            "Party snapshot TCP request type is incorrect.");
        int snapshotRequestPayloadSize =
            snapshotRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var snapshotRequestPayload = new byte[snapshotRequestPayloadSize];
        await serverStream.ReadExactlyAsync(snapshotRequestPayload, timeout.Token);
        var snapshotRequestBuffer = new ByteBuffer(snapshotRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(snapshotRequestBuffer),
            "Party snapshot TCP request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(snapshotRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.PartySnapshotRequest,
            "Party snapshot TCP request FlatBuffer type is incorrect.");

        byte[] snapshotResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.PartySnapshotResponse,
            snapshotRequestHeader.RequestId,
            CreatePartySnapshotResponseBytes(
                TcpSchema.PartySnapshotResult.Success,
                9,
                42,
                new ulong[] { 42, 43 }));
        await serverStream.WriteAsync(snapshotResponseBytes, timeout.Token);

        TcpPacket snapshotResponse = await snapshotResponseTask;
        Assert(snapshotResponse.Header.Type == TcpPacketType.PartySnapshotResponse,
            "Party snapshot TCP response type is incorrect.");

        Task<TcpPacket> enterDungeonResponseTask = connection.SendRequestAsync(
            TcpPacketType.EnterDungeonRequest,
            GamePayloadCodec.EncodeEnterDungeonRequest(1001),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader enterDungeonRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        int enterDungeonRequestPayloadSize =
            enterDungeonRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var enterDungeonRequestPayload =
            new byte[enterDungeonRequestPayloadSize];
        await serverStream.ReadExactlyAsync(
            enterDungeonRequestPayload,
            timeout.Token);
        Assert(enterDungeonRequestHeader.Type == TcpPacketType.EnterDungeonRequest,
            "Enter dungeon TCP request type is incorrect.");
        var enterDungeonRequestBuffer =
            new ByteBuffer(enterDungeonRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(enterDungeonRequestBuffer),
            "Enter dungeon TCP request FlatBuffer is invalid.");
        TcpSchema.TcpMessage enterDungeonRequest =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(enterDungeonRequestBuffer);
        Assert(enterDungeonRequest.PayloadType ==
            TcpSchema.TcpPayload.EnterDungeonRequest &&
            enterDungeonRequest.PayloadAsEnterDungeonRequest()
                .DungeonTemplateId == 1001,
            "Enter dungeon TCP request payload is incorrect.");

        byte[] enterDungeonResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.EnterDungeonResponse,
            enterDungeonRequestHeader.RequestId,
            EnterDungeonResponseBytes(
                TcpSchema.EnterDungeonResult.Success,
                10,
                40000,
                77));
        await serverStream.WriteAsync(enterDungeonResponseBytes, timeout.Token);

        TcpPacket enterDungeonResponse = await enterDungeonResponseTask;
        EnterDungeonResponse receivedDungeon =
            GamePayloadCodec.DecodeEnterDungeonResponse(
                enterDungeonResponse.Payload);
        Assert(receivedDungeon.Result == EnterDungeonResult.Success &&
            receivedDungeon.DungeonId == 10 &&
            receivedDungeon.UdpPort == 40000 &&
            receivedDungeon.UdpToken == 77,
            "Enter dungeon TCP response payload is incorrect.");

        Task<TcpPacket> staticDataResponseTask = connection.SendRequestAsync(
            TcpPacketType.DungeonStaticDataRequest,
            DungeonStaticDataCodec.EncodeRequest(receivedDungeon.DungeonId),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader staticDataRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        int staticDataRequestPayloadSize =
            staticDataRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var staticDataRequestPayload =
            new byte[staticDataRequestPayloadSize];
        await serverStream.ReadExactlyAsync(
            staticDataRequestPayload,
            timeout.Token);
        Assert(staticDataRequestHeader.Type ==
            TcpPacketType.DungeonStaticDataRequest,
            "Dungeon static data TCP request type is incorrect.");
        var staticDataRequestBuffer = new ByteBuffer(staticDataRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(staticDataRequestBuffer),
            "Dungeon static data TCP request FlatBuffer is invalid.");
        TcpSchema.TcpMessage staticDataRequest =
            TcpSchema.TcpMessage.GetRootAsTcpMessage(staticDataRequestBuffer);
        Assert(staticDataRequest.PayloadType ==
            TcpSchema.TcpPayload.DungeonStaticDataRequest &&
            staticDataRequest.PayloadAsDungeonStaticDataRequest().DungeonId == 10,
            "Dungeon static data TCP request payload is incorrect.");

        byte[] staticDataResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.DungeonStaticDataResponse,
            staticDataRequestHeader.RequestId,
            DungeonStaticDataResponseBytes(
                TcpSchema.DungeonStaticDataResult.Success,
                10,
                1001));
        await serverStream.WriteAsync(staticDataResponseBytes, timeout.Token);

        TcpPacket staticDataResponse = await staticDataResponseTask;
        DungeonStaticData receivedStaticData =
            DungeonStaticDataCodec.DecodeResponse(staticDataResponse.Payload);
        Assert(staticDataResponse.Header.Type ==
            TcpPacketType.DungeonStaticDataResponse &&
            receivedStaticData.Rooms.Count == 2 &&
            receivedStaticData.EnemyTemplates.Count == 1,
            "Dungeon static data TCP response is incorrect.");

        Task<TcpPacket> connectionInfoResponseTask = connection.SendRequestAsync(
            TcpPacketType.DungeonConnectionInfoRequest,
            GamePayloadCodec.EncodeDungeonConnectionInfoRequest(),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader connectionInfoRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        int connectionInfoRequestPayloadSize =
            connectionInfoRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var connectionInfoRequestPayload =
            new byte[connectionInfoRequestPayloadSize];
        await serverStream.ReadExactlyAsync(
            connectionInfoRequestPayload,
            timeout.Token);
        Assert(connectionInfoRequestHeader.Type ==
            TcpPacketType.DungeonConnectionInfoRequest,
            "Dungeon connection info TCP request type is incorrect.");
        var connectionInfoRequestBuffer =
            new ByteBuffer(connectionInfoRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(connectionInfoRequestBuffer),
            "Dungeon connection info TCP request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(connectionInfoRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.DungeonConnectionInfoRequest,
            "Dungeon connection info TCP request FlatBuffer type is incorrect.");

        byte[] connectionInfoResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.DungeonConnectionInfoResponse,
            connectionInfoRequestHeader.RequestId,
            DungeonConnectionInfoResponseBytes(
                TcpSchema.DungeonConnectionInfoResult.Success,
                10,
                40000,
                88));
        await serverStream.WriteAsync(connectionInfoResponseBytes, timeout.Token);

        TcpPacket connectionInfoResponse = await connectionInfoResponseTask;
        DungeonConnectionInfo receivedConnectionInfo =
            GamePayloadCodec.DecodeDungeonConnectionInfoResponse(
                connectionInfoResponse.Payload);
        Assert(receivedConnectionInfo.Result ==
            DungeonConnectionInfoResult.Success &&
            receivedConnectionInfo.DungeonId == 10 &&
            receivedConnectionInfo.UdpPort == 40000 &&
            receivedConnectionInfo.UdpToken == 88,
            "Dungeon connection info TCP response payload is incorrect.");

        Task<TcpPacket> leavePartyResponseTask = connection.SendRequestAsync(
            TcpPacketType.LeavePartyRequest,
            GamePayloadCodec.EncodeLeavePartyRequest(),
            timeout.Token);

        await serverStream.ReadExactlyAsync(requestHeaderBytes, timeout.Token);
        TcpPacketHeader leavePartyRequestHeader =
            TcpPacketCodec.DecodeHeader(requestHeaderBytes);
        Assert(leavePartyRequestHeader.Type == TcpPacketType.LeavePartyRequest,
            "Leave party TCP request type is incorrect.");
        int leavePartyRequestPayloadSize =
            leavePartyRequestHeader.PacketSize - TcpPacketCodec.HeaderSize;
        var leavePartyRequestPayload = new byte[leavePartyRequestPayloadSize];
        await serverStream.ReadExactlyAsync(leavePartyRequestPayload, timeout.Token);
        var leavePartyRequestBuffer = new ByteBuffer(leavePartyRequestPayload);
        Assert(TcpSchema.TcpMessage.VerifyTcpMessage(leavePartyRequestBuffer),
            "Leave party TCP request FlatBuffer is invalid.");
        Assert(TcpSchema.TcpMessage.GetRootAsTcpMessage(leavePartyRequestBuffer)
            .PayloadType == TcpSchema.TcpPayload.LeavePartyRequest,
            "Leave party TCP request FlatBuffer type is incorrect.");

        byte[] leavePartyResponseBytes = TcpPacketCodec.EncodePacket(
            TcpPacketType.LeavePartyResponse,
            leavePartyRequestHeader.RequestId,
            LeavePartyResponseBytes(TcpSchema.LeavePartyResult.Success));
        await serverStream.WriteAsync(leavePartyResponseBytes, timeout.Token);

        TcpPacket leavePartyResponse = await leavePartyResponseTask;
        Assert(leavePartyResponse.Header.Type == TcpPacketType.LeavePartyResponse,
            "Leave party TCP response type is incorrect.");

        connection.Disconnect();

        if (connection.IsConnected)
        {
            throw new InvalidOperationException("TCP connection was not closed.");
        }
    }

}
