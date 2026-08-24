using DnfMockClient.LoadTest;

ParseResult parseResult;
try
{
    parseResult = LoadTestOptions.Parse(args);
}
catch (ArgumentException exception)
{
    Console.Error.WriteLine($"Configuration error: {exception.Message}");
    return 2;
}

if (parseResult.ShowHelp)
{
    LoadTestOptions.PrintUsage(Console.Out);
    return 0;
}

if (parseResult.Error is not null || parseResult.Options is null)
{
    Console.Error.WriteLine($"Configuration error: {parseResult.Error}");
    Console.Error.WriteLine("Use --help to see valid options.");
    return 2;
}

LoadTestOptions options = parseResult.Options;
Console.WriteLine("DNF load test");
Console.WriteLine($"  profile:     {options.Profile.ToString().ToLowerInvariant()}");
Console.WriteLine($"  accounts:    {options.AccountCount}");
Console.WriteLine($"  concurrency: {options.Concurrency}");
Console.WriteLine($"  hold:        {options.Duration.TotalSeconds:F0} s per session");
Console.WriteLine($"  timeout:     {options.SessionTimeout.TotalSeconds:F0} s for setup");
Console.WriteLine("  credentials: supplied through environment (not displayed)");

if (options.Profile == LoadTestProfile.Dungeon && options.Concurrency % 4 != 0)
{
    Console.WriteLine(
        $"  effective dungeon concurrency: {(options.Concurrency / 4) * 4} " +
        "(complete four-player parties only)");
}

if (options.ValidateOnly)
{
    Console.WriteLine("Configuration is valid; no network connection was attempted.");
    return 0;
}

using var cancellation = new CancellationTokenSource();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    cancellation.Cancel();
};

try
{
    var runner = new LoadTestRunner(options);
    LoadTestReport report = await runner.RunAsync(cancellation.Token);
    report.Print(Console.Out);
    return options.FailOnSessionError && report.Failed > 0 ? 1 : 0;
}
catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
{
    Console.Error.WriteLine("Load test cancelled.");
    return 130;
}
catch (Exception exception)
{
    Console.Error.WriteLine(
        $"Load-test infrastructure error: {exception.GetType().Name}");
    return 1;
}
