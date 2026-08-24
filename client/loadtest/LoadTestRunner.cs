using System.Diagnostics;

namespace DnfMockClient.LoadTest;

internal sealed class LoadTestRunner
{
    private readonly LoadTestOptions _options;
    private readonly LoadTestMetrics _metrics = new();
    private readonly object _progressLock = new();
    private readonly int _progressStep;

    public LoadTestRunner(LoadTestOptions options)
    {
        _options = options;
        _progressStep = Math.Max(1, options.AccountCount / 10);
    }

    public async Task<LoadTestReport> RunAsync(
        CancellationToken cancellationToken)
    {
        var stopwatch = Stopwatch.StartNew();
        if (_options.Profile == LoadTestProfile.Lobby)
        {
            await RunLobbyAsync(cancellationToken);
        }
        else
        {
            await RunDungeonAsync(cancellationToken);
        }

        stopwatch.Stop();
        return _metrics.CreateReport(_options, stopwatch.Elapsed);
    }

    private async Task RunLobbyAsync(CancellationToken cancellationToken)
    {
        using var semaphore = new SemaphoreSlim(_options.Concurrency);
        Task[] sessions = Enumerable.Range(0, _options.AccountCount)
            .Select(index => RunLobbySessionAsync(
                index,
                semaphore,
                cancellationToken))
            .ToArray();
        await Task.WhenAll(sessions);
    }

    private async Task RunLobbySessionAsync(
        int index,
        SemaphoreSlim semaphore,
        CancellationToken cancellationToken)
    {
        await semaphore.WaitAsync(cancellationToken);
        _metrics.SessionStarted();
        var recorder = new AttemptRecorder();
        using var client = new LoadTestClient(
            _options.LoginId(index),
            _options.Password);
        using var setupCancellation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        setupCancellation.CancelAfter(_options.SessionTimeout);

        AttemptResult result;
        try
        {
            await client.ConnectLobbyAsync(
                _options,
                recorder,
                setupCancellation.Token);
            recorder.CompleteSetup();
            recorder.BeginStage(LoadTestStage.Sustain);
            await client.SustainLobbyAsync(
                _options.Duration,
                _options.SessionTimeout,
                cancellationToken);
            result = recorder.Success();
        }
        catch (Exception exception) when (
            exception is not OperationCanceledException ||
            !cancellationToken.IsCancellationRequested)
        {
            result = recorder.Failure(exception);
        }
        finally
        {
            _metrics.SessionStopped();
            semaphore.Release();
        }

        AddResult(result);
    }

    private async Task RunDungeonAsync(CancellationToken cancellationToken)
    {
        int maximumGroups = Math.Max(1, _options.Concurrency / 4);
        using var semaphore = new SemaphoreSlim(maximumGroups);
        int groupCount = _options.AccountCount / 4;
        Task[] groups = Enumerable.Range(0, groupCount)
            .Select(groupIndex => RunDungeonGroupAsync(
                groupIndex,
                semaphore,
                cancellationToken))
            .ToArray();
        await Task.WhenAll(groups);
    }

    private async Task RunDungeonGroupAsync(
        int groupIndex,
        SemaphoreSlim semaphore,
        CancellationToken cancellationToken)
    {
        await semaphore.WaitAsync(cancellationToken);
        var clients = new LoadTestClient[4];
        var recorders = new AttemptRecorder[4];
        var results = new AttemptResult?[4];

        for (int memberIndex = 0; memberIndex < clients.Length; memberIndex++)
        {
            int accountIndex = groupIndex * 4 + memberIndex;
            clients[memberIndex] = new LoadTestClient(
                _options.LoginId(accountIndex),
                _options.Password);
            recorders[memberIndex] = new AttemptRecorder();
            _metrics.SessionStarted();
        }

        using var setupCancellation = CancellationTokenSource.CreateLinkedTokenSource(
            cancellationToken);
        setupCancellation.CancelAfter(_options.SessionTimeout);
        try
        {
            await ConnectDungeonGroupAsync(
                clients,
                recorders,
                results,
                setupCancellation.Token);
            foreach (AttemptRecorder recorder in recorders)
            {
                recorder.CompleteSetup();
                recorder.BeginStage(LoadTestStage.Sustain);
            }

            Task[] sustainTasks = clients.Select((client, index) =>
                CaptureFailureAsync(
                    () => client.SustainDungeonAsync(
                        _options.Duration,
                        cancellationToken),
                    index,
                    recorders,
                    results,
                    cancellationToken))
                .ToArray();
            await Task.WhenAll(sustainTasks);
            ThrowIfAnyFailed(results, "a dungeon session failed during sustain");

            for (int index = 0; index < results.Length; index++)
            {
                results[index] = recorders[index].Success();
            }
        }
        catch (Exception exception) when (
            exception is not OperationCanceledException ||
            !cancellationToken.IsCancellationRequested)
        {
            for (int index = 0; index < results.Length; index++)
            {
                results[index] ??= recorders[index].DependencyFailure(
                    recorders[index].CurrentStage);
            }
        }
        finally
        {
            foreach (LoadTestClient client in clients)
            {
                client.Dispose();
                _metrics.SessionStopped();
            }

            semaphore.Release();
        }

        foreach (AttemptResult? result in results)
        {
            AddResult(result ?? throw new InvalidOperationException(
                "Dungeon group did not produce a result."));
        }
    }

    private async Task ConnectDungeonGroupAsync(
        LoadTestClient[] clients,
        AttemptRecorder[] recorders,
        AttemptResult?[] results,
        CancellationToken cancellationToken)
    {
        Task[] lobbyTasks = clients.Select((client, index) =>
            CaptureFailureAsync(
                () => client.ConnectLobbyAsync(
                    _options,
                    recorders[index],
                    cancellationToken),
                index,
                recorders,
                results,
                cancellationToken))
            .ToArray();
        await Task.WhenAll(lobbyTasks);
        ThrowIfAnyFailed(results, "a party member failed to enter the lobby");

        BeginGroupStage(recorders, results, LoadTestStage.PartyCreate);
        ulong partyId;
        try
        {
            partyId = await clients[0].CreatePartyAsync(
                recorders[0],
                cancellationToken);
        }
        catch (Exception exception)
        {
            results[0] = recorders[0].Failure(exception);
            throw;
        }

        BeginGroupStage(recorders, results, LoadTestStage.PartyJoin);
        Task[] joinTasks = clients.Skip(1).Select((client, offset) =>
            CaptureFailureAsync(
                () => client.JoinPartyAsync(
                    partyId,
                    recorders[offset + 1],
                    cancellationToken),
                offset + 1,
                recorders,
                results,
                cancellationToken))
            .ToArray();
        await Task.WhenAll(joinTasks);
        ThrowIfAnyFailed(results, "a party member failed to join the party");

        BeginGroupStage(recorders, results, LoadTestStage.DungeonCreate);
        DungeonEndpoint leaderEndpoint;
        try
        {
            leaderEndpoint = await clients[0].CreateDungeonAsync(
                recorders[0],
                cancellationToken);
        }
        catch (Exception exception)
        {
            results[0] = recorders[0].Failure(exception);
            throw;
        }

        var endpoints = new DungeonEndpoint[4];
        endpoints[0] = leaderEndpoint;
        BeginGroupStage(recorders, results, LoadTestStage.DungeonLookup);
        Task[] lookupTasks = clients.Skip(1).Select((client, offset) =>
            CaptureFailureAsync(
                async () =>
                {
                    endpoints[offset + 1] =
                        await client.GetDungeonEndpointAsync(
                            recorders[offset + 1],
                            cancellationToken);
                },
                offset + 1,
                recorders,
                results,
                cancellationToken))
            .ToArray();
        await Task.WhenAll(lookupTasks);
        ThrowIfAnyFailed(results, "a party member failed dungeon lookup");

        BeginGroupStage(recorders, results, LoadTestStage.StaticData);
        Task[] udpTasks = clients.Select((client, index) =>
            CaptureFailureAsync(
                () => client.ConnectDungeonAsync(
                    endpoints[index],
                    recorders[index],
                    cancellationToken),
                index,
                recorders,
                results,
                cancellationToken))
            .ToArray();
        await Task.WhenAll(udpTasks);
        ThrowIfAnyFailed(results, "a party member failed UDP authentication");
    }

    private static async Task CaptureFailureAsync(
        Func<Task> operation,
        int index,
        AttemptRecorder[] recorders,
        AttemptResult?[] results,
        CancellationToken cancellationToken)
    {
        try
        {
            await operation();
        }
        catch (Exception exception)
        {
            results[index] = recorders[index].Failure(exception);
            if (exception is OperationCanceledException &&
                cancellationToken.IsCancellationRequested)
            {
                throw;
            }
        }
    }

    private static void BeginGroupStage(
        AttemptRecorder[] recorders,
        AttemptResult?[] results,
        LoadTestStage stage)
    {
        for (int index = 0; index < recorders.Length; index++)
        {
            if (results[index] is null)
            {
                recorders[index].BeginStage(stage);
            }
        }
    }

    private static void ThrowIfAnyFailed(
        AttemptResult?[] results,
        string message)
    {
        if (results.Any(result => result is not null))
        {
            throw new InvalidOperationException(message);
        }
    }

    private void AddResult(AttemptResult result)
    {
        _metrics.Add(result);
        int completed = _metrics.Completed;
        if (completed == _options.AccountCount ||
            completed % _progressStep == 0)
        {
            lock (_progressLock)
            {
                Console.WriteLine(
                    $"Progress: {completed}/{_options.AccountCount} sessions completed");
            }
        }
    }
}
