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
public sealed class DungeonUdpServiceTests
{
    [Fact]
    public async Task AuthenticatesAndExchangesDungeonDatagrams()
    {
        using var server = new UdpClient(new IPEndPoint(IPAddress.Loopback, 0));
        int port = ((IPEndPoint)server.Client.LocalEndPoint!).Port;
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(3));
        using var client = new DungeonUdpService();
        var snapshotCompletion =
            new TaskCompletionSource<DungeonSnapshotData>(
                TaskCreationOptions.RunContinuationsAsynchronously);

        client.SnapshotReceived += snapshot => snapshotCompletion.TrySetResult(snapshot);
        Task<UdpHelloAckData> connectTask = client.ConnectAsync(
            "127.0.0.1",
            port,
            9,
            3,
            77,
            timeout.Token);

        UdpReceiveResult helloResult = await server.ReceiveAsync(timeout.Token);
        var helloBuffer = new ByteBuffer(helloResult.Buffer);
        DungeonMessage helloMessage = DungeonMessage.GetRootAsDungeonMessage(helloBuffer);
        Assert(helloMessage.PayloadType == DungeonPayload.UdpHello,
            "UDP service did not send hello first.");
        Assert(client.IsRunning && !client.IsAuthenticated && !connectTask.IsCompleted,
            "UDP service authenticated before receiving an acknowledgement.");

        byte[] snapshotBytes = CreateTestSnapshotBytes();
        await server.SendAsync(snapshotBytes, helloResult.RemoteEndPoint, timeout.Token);
        await Task.Delay(TimeSpan.FromMilliseconds(30), timeout.Token);
        Assert(!snapshotCompletion.Task.IsCompleted,
            "UDP service published a snapshot before authentication.");

        byte[] ackBytes = CreateUdpHelloAckBytes(
            UdpHelloAckResult.Success,
            44);
        await server.SendAsync(ackBytes, helloResult.RemoteEndPoint, timeout.Token);
        UdpHelloAckData ack = await connectTask;
        Assert(ack.Result == UdpHelloResult.Success && ack.ServerTick == 44 &&
            client.IsAuthenticated,
            "UDP service did not complete successful authentication.");

        await server.SendAsync(snapshotBytes, helloResult.RemoteEndPoint, timeout.Token);
        DungeonSnapshotData receivedSnapshot =
            await snapshotCompletion.Task.WaitAsync(timeout.Token);
        Assert(receivedSnapshot.ServerTick == 45,
            "UDP service did not publish the received snapshot.");

        await client.SendMovementAsync(1.0f, 0.0f, false, timeout.Token);
        UdpReceiveResult movementResult = await server.ReceiveAsync(timeout.Token);
        var movementBuffer = new ByteBuffer(movementResult.Buffer);
        DungeonMessage movementMessage =
            DungeonMessage.GetRootAsDungeonMessage(movementBuffer);
        Assert(movementMessage.PayloadType == DungeonPayload.PlayerMovement &&
            movementMessage.PayloadAsPlayerMovement().Sequence == 1,
            "UDP service movement sequence is incorrect.");

        await client.SendAttackAsync(1001, 1.0f, 0.0f, timeout.Token);
        uint? retriedAttackSequence = null;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            UdpReceiveResult attackResult = await server.ReceiveAsync(timeout.Token);
            var attackBuffer = new ByteBuffer(attackResult.Buffer);
            DungeonMessage attackMessage =
                DungeonMessage.GetRootAsDungeonMessage(attackBuffer);
            Assert(attackMessage.PayloadType == DungeonPayload.PlayerAttack,
                "UDP attack retry emitted an unexpected payload type.");

            uint sequence = attackMessage.PayloadAsPlayerAttack().Sequence;
            retriedAttackSequence ??= sequence;
            Assert(sequence == retriedAttackSequence.Value,
                "UDP attack retries must reuse one sequence number.");
        }

        Assert(retriedAttackSequence == 1,
            "UDP service attack sequence is incorrect.");

        client.Disconnect();
        Assert(!client.IsRunning && !client.IsAuthenticated,
            "UDP service did not disconnect.");

        using var failureServer = new UdpClient(
            new IPEndPoint(IPAddress.Loopback, 0));
        int failurePort =
            ((IPEndPoint)failureServer.Client.LocalEndPoint!).Port;
        using var failureTimeout =
            new CancellationTokenSource(TimeSpan.FromSeconds(3));
        Task<UdpHelloAckData> failedConnectTask = client.ConnectAsync(
            "127.0.0.1",
            failurePort,
            9,
            3,
            77,
            failureTimeout.Token);
        UdpReceiveResult failedHello =
            await failureServer.ReceiveAsync(failureTimeout.Token);
        byte[] failedAckBytes = CreateUdpHelloAckBytes(
            UdpHelloAckResult.AuthenticationFailed,
            0);
        await failureServer.SendAsync(
            failedAckBytes,
            failedHello.RemoteEndPoint,
            failureTimeout.Token);

        bool authenticationRejected = false;
        try
        {
            await failedConnectTask;
        }
        catch (InvalidDataException)
        {
            authenticationRejected = true;
        }

        Assert(authenticationRejected &&
            !client.IsRunning &&
            !client.IsAuthenticated,
            "UDP authentication failure did not close the client session.");
    }

}
