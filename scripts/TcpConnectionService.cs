using System;
using System.Net.Sockets;
using System.Threading;
using System.Threading.Tasks;

namespace DnfMockClient.Networking;

public sealed class TcpConnectionService : IDisposable
{
    private TcpClient? _client;

    public bool IsConnected => _client?.Connected == true;

    public async Task ConnectAsync(
        string host,
        int port,
        CancellationToken cancellationToken)
    {
        if (string.IsNullOrWhiteSpace(host))
        {
            throw new ArgumentException("Server address is required.", nameof(host));
        }

        if (port is < 1 or > 65535)
        {
            throw new ArgumentOutOfRangeException(nameof(port));
        }

        Disconnect();

        var client = new TcpClient();

        try
        {
            await client.ConnectAsync(host, port, cancellationToken);
            _client = client;
        }
        catch
        {
            client.Dispose();
            throw;
        }
    }

    public void Disconnect()
    {
        _client?.Dispose();
        _client = null;
    }

    public void Dispose()
    {
        Disconnect();
    }
}
