using System.Net;
using System.Net.Sockets;
using DnfMockClient.Networking;

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

connection.Disconnect();

if (connection.IsConnected)
{
    throw new InvalidOperationException("TCP connection was not closed.");
}

Console.WriteLine("TCP connection smoke test passed.");
