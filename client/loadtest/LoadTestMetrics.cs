using System.Collections.Concurrent;
using System.Diagnostics;
using System.Globalization;

namespace DnfMockClient.LoadTest;

internal enum LoadTestStage
{
    TlsConnect,
    AuthLogin,
    CharacterList,
    CharacterSelection,
    GameConnect,
    GameLogin,
    ChannelList,
    ChannelJoin,
    PartyCreate,
    PartyJoin,
    DungeonCreate,
    DungeonLookup,
    StaticData,
    UdpHello,
    Sustain,
    EndToEnd
}

internal sealed class AttemptRecorder
{
    private readonly Stopwatch _total = Stopwatch.StartNew();
    private readonly Dictionary<LoadTestStage, double> _latencies = new();
    private LoadTestStage _currentStage = LoadTestStage.TlsConnect;

    public LoadTestStage CurrentStage => _currentStage;

    public async Task MeasureAsync(
        LoadTestStage stage,
        Func<Task> operation)
    {
        _currentStage = stage;
        var stopwatch = Stopwatch.StartNew();
        try
        {
            await operation();
        }
        finally
        {
            stopwatch.Stop();
            AddLatency(stage, stopwatch.Elapsed.TotalMilliseconds);
        }
    }

    public async Task<T> MeasureAsync<T>(
        LoadTestStage stage,
        Func<Task<T>> operation)
    {
        _currentStage = stage;
        var stopwatch = Stopwatch.StartNew();
        try
        {
            return await operation();
        }
        finally
        {
            stopwatch.Stop();
            AddLatency(stage, stopwatch.Elapsed.TotalMilliseconds);
        }
    }

    public AttemptResult Success()
    {
        return Finish(success: true, null, null);
    }

    public void CompleteSetup()
    {
        if (!_latencies.ContainsKey(LoadTestStage.EndToEnd))
        {
            _latencies[LoadTestStage.EndToEnd] =
                _total.Elapsed.TotalMilliseconds;
        }
    }

    public void BeginStage(LoadTestStage stage)
    {
        _currentStage = stage;
    }

    public AttemptResult Failure(Exception exception)
    {
        return Finish(
            success: false,
            _currentStage,
            exception.GetType().Name);
    }

    public AttemptResult DependencyFailure(LoadTestStage stage)
    {
        _currentStage = stage;
        return Finish(
            success: false,
            stage,
            "GroupDependencyFailure");
    }

    private AttemptResult Finish(
        bool success,
        LoadTestStage? failureStage,
        string? failureKind)
    {
        _total.Stop();
        CompleteSetup();
        return new AttemptResult(
            success,
            new Dictionary<LoadTestStage, double>(_latencies),
            failureStage,
            failureKind);
    }

    private void AddLatency(LoadTestStage stage, double elapsedMilliseconds)
    {
        if (_latencies.TryGetValue(stage, out double previous))
        {
            _latencies[stage] = previous + elapsedMilliseconds;
        }
        else
        {
            _latencies[stage] = elapsedMilliseconds;
        }
    }
}

internal sealed record AttemptResult(
    bool Success,
    IReadOnlyDictionary<LoadTestStage, double> Latencies,
    LoadTestStage? FailureStage,
    string? FailureKind);

internal sealed class LoadTestMetrics
{
    private readonly ConcurrentBag<AttemptResult> _results = new();
    private int _activeSessions;
    private int _peakSessions;
    private int _completed;

    public int Completed => Volatile.Read(ref _completed);
    public int PeakSessions => Volatile.Read(ref _peakSessions);

    public void SessionStarted()
    {
        int active = Interlocked.Increment(ref _activeSessions);
        int peak = Volatile.Read(ref _peakSessions);
        while (active > peak)
        {
            int previous = Interlocked.CompareExchange(
                ref _peakSessions,
                active,
                peak);
            if (previous == peak)
            {
                break;
            }

            peak = previous;
        }
    }

    public void SessionStopped()
    {
        Interlocked.Decrement(ref _activeSessions);
    }

    public void Add(AttemptResult result)
    {
        _results.Add(result);
        Interlocked.Increment(ref _completed);
    }

    public LoadTestReport CreateReport(
        LoadTestOptions options,
        TimeSpan wallTime)
    {
        AttemptResult[] results = _results.ToArray();
        int succeeded = results.Count(result => result.Success);
        int failed = results.Length - succeeded;

        var latencyRows = Enum.GetValues<LoadTestStage>()
            .Select(stage =>
            {
                double[] samples = results
                    .Where(result => result.Latencies.ContainsKey(stage))
                    .Select(result => result.Latencies[stage])
                    .OrderBy(value => value)
                    .ToArray();
                return new LatencyRow(
                    stage,
                    samples.Length,
                    Percentile(samples, 0.50),
                    Percentile(samples, 0.95),
                    Percentile(samples, 0.99));
            })
            .Where(row => row.Samples > 0)
            .ToArray();

        IReadOnlyList<FailureRow> failures = results
            .Where(result => !result.Success)
            .GroupBy(result => new
            {
                result.FailureStage,
                result.FailureKind
            })
            .Select(group => new FailureRow(
                group.Key.FailureStage?.ToString() ?? "Unknown",
                group.Key.FailureKind ?? "Unknown",
                group.Count()))
            .OrderByDescending(row => row.Count)
            .ThenBy(row => row.Stage, StringComparer.Ordinal)
            .ToArray();

        double throughput = wallTime.TotalSeconds > 0.0
            ? results.Length / wallTime.TotalSeconds
            : 0.0;

        return new LoadTestReport(
            options,
            results.Length,
            succeeded,
            failed,
            PeakSessions,
            wallTime,
            throughput,
            latencyRows,
            failures);
    }

    private static double Percentile(double[] sortedValues, double percentile)
    {
        if (sortedValues.Length == 0)
        {
            return 0.0;
        }

        int rank = (int)Math.Ceiling(percentile * sortedValues.Length);
        return sortedValues[Math.Clamp(rank - 1, 0, sortedValues.Length - 1)];
    }
}

internal sealed record LatencyRow(
    LoadTestStage Stage,
    int Samples,
    double P50,
    double P95,
    double P99);

internal sealed record FailureRow(string Stage, string Kind, int Count);

internal sealed record LoadTestReport(
    LoadTestOptions Options,
    int Attempts,
    int Succeeded,
    int Failed,
    int PeakSessions,
    TimeSpan WallTime,
    double Throughput,
    IReadOnlyList<LatencyRow> Latencies,
    IReadOnlyList<FailureRow> Failures)
{
    public void Print(TextWriter output)
    {
        output.WriteLine();
        output.WriteLine("Load-test summary");
        output.WriteLine($"  profile:       {Options.Profile.ToString().ToLowerInvariant()}");
        output.WriteLine($"  attempted:     {Attempts}");
        output.WriteLine($"  succeeded:     {Succeeded}");
        output.WriteLine($"  failed:        {Failed}");
        output.WriteLine($"  peak sessions: {PeakSessions}");
        output.WriteLine($"  wall time:     {WallTime.TotalSeconds:F2} s");
        output.WriteLine($"  throughput:    {Throughput:F2} completed sessions/s");
        output.WriteLine();
        output.WriteLine("Latency (ms, nearest-rank percentiles; hold time excluded)");
        output.WriteLine("  stage                 samples       p50       p95       p99");
        foreach (LatencyRow row in Latencies)
        {
            output.WriteLine(string.Format(
                CultureInfo.InvariantCulture,
                "  {0,-21} {1,7} {2,9:F2} {3,9:F2} {4,9:F2}",
                row.Stage,
                row.Samples,
                row.P50,
                row.P95,
                row.P99));
        }

        if (Failures.Count > 0)
        {
            output.WriteLine();
            output.WriteLine("Failures (no account or secret values are logged)");
            foreach (FailureRow failure in Failures)
            {
                output.WriteLine(
                    $"  {failure.Stage}/{failure.Kind}: {failure.Count}");
            }
        }
    }
}
