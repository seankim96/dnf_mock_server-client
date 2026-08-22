using DnfMockClient.MultiplayerDemo;
using DnfMockClient.Protocol;

try
{
    MultiplayerDemoSettings settings =
        MultiplayerDemoSettings.FromEnvironment();
    using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(90));
    var clients = new List<DemoClient>();

    try
    {
        for (int index = 0; index < settings.ClientCount; index++)
        {
            clients.Add(new DemoClient(
                settings.LoginId(index),
                settings.Password,
                index));
        }

        Console.WriteLine(
            $"Starting {clients.Count} independent account sessions...");

        await Task.WhenAll(clients.Select(client => client.ConnectLobbyAsync(
            settings.AuthHost,
            settings.AuthPort,
            settings.CertificateFingerprint,
            timeout.Token)));

        DemoClient leader = clients[0];
        ulong partyId = await leader.CreatePartyAsync(timeout.Token);
        await Task.WhenAll(clients.Skip(1).Select(
            client => client.JoinPartyAsync(partyId, timeout.Token)));

        PartySnapshotData party = await leader.GetPartyAsync(timeout.Token);
        if (party.Members.Count != clients.Count)
        {
            throw new InvalidOperationException(
                $"Party has {party.Members.Count} members, " +
                $"expected {clients.Count}.");
        }

        Console.WriteLine(
            $"Party {party.PartyId} ready with {party.Members.Count} accounts.");

        DungeonEndpoint leaderEndpoint =
            await leader.CreateDungeonAsync(timeout.Token);
        var endpoints = new DungeonEndpoint[clients.Count];
        endpoints[0] = leaderEndpoint;

        DungeonEndpoint[] memberEndpoints = await Task.WhenAll(
            clients.Skip(1).Select(
                client => client.GetDungeonEndpointAsync(timeout.Token)));
        Array.Copy(memberEndpoints, 0, endpoints, 1, memberEndpoints.Length);

        await Task.WhenAll(clients.Select((client, index) =>
            client.ConnectDungeonAsync(endpoints[index], timeout.Token)));

        Console.WriteLine(
            "All accounts passed individual UDP authentication. Autoplay started.");

        await Task.WhenAll(clients.Select(
            client => client.AutoPlayAsync(timeout.Token)));

        Console.WriteLine(
            $"Multiplayer demo completed: {clients.Count} accounts cleared together.");
    }
    finally
    {
        foreach (DemoClient client in clients)
        {
            client.Dispose();
        }
    }
}
catch (Exception exception)
{
    Console.Error.WriteLine($"Multiplayer demo failed: {exception.Message}");
    return 1;
}

return 0;
