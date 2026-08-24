using System.Globalization;
using DnfMockClient.Networking;

namespace DnfMockClient.LoadTest;

internal enum LoadTestProfile
{
    Lobby,
    Dungeon
}

internal sealed class LoadTestOptions
{
    private const int MaximumAccounts = 1000;
    private const int MaximumDurationSeconds = 3600;

    public required LoadTestProfile Profile { get; init; }
    public required int AccountCount { get; init; }
    public required int Concurrency { get; init; }
    public required TimeSpan Duration { get; init; }
    public required TimeSpan SessionTimeout { get; init; }
    public required string AuthHost { get; init; }
    public required int AuthPort { get; init; }
    public required string CertificateFingerprint { get; init; }
    public required string AccountPrefix { get; init; }
    public required string Password { get; init; }
    public required bool FailOnSessionError { get; init; }
    public required bool ValidateOnly { get; init; }

    public string LoginId(int zeroBasedIndex)
    {
        return $"{AccountPrefix}{zeroBasedIndex + 1:D4}";
    }

    public static ParseResult Parse(string[] arguments)
    {
        var values = new Dictionary<string, string>(StringComparer.Ordinal);
        bool failOnSessionError = ReadBooleanEnvironment(
            "DNF_LOADTEST_FAIL_ON_SESSION_ERROR",
            defaultValue: false);
        bool validateOnly = false;

        for (int index = 0; index < arguments.Length; index++)
        {
            string argument = arguments[index];
            if (argument is "--help" or "-h")
            {
                return ParseResult.Help();
            }

            if (argument == "--fail-on-session-error")
            {
                failOnSessionError = true;
                continue;
            }

            if (argument == "--validate-only")
            {
                validateOnly = true;
                continue;
            }

            if (!KnownValueOptions.Contains(argument))
            {
                return ParseResult.Failure($"Unknown option: {argument}");
            }

            if (++index >= arguments.Length)
            {
                return ParseResult.Failure($"Missing value for {argument}.");
            }

            values[argument] = arguments[index];
        }

        try
        {
            LoadTestProfile profile = ParseProfile(Value(
                values,
                "--profile",
                "DNF_LOADTEST_PROFILE",
                "lobby"));
            int accountCount = ParseInteger(
                Value(values, "--accounts", "DNF_LOADTEST_ACCOUNTS", "100"),
                "accounts",
                1,
                MaximumAccounts);
            int concurrency = ParseInteger(
                Value(values, "--concurrency", "DNF_LOADTEST_CONCURRENCY", "20"),
                "concurrency",
                1,
                MaximumAccounts);
            int durationSeconds = ParseInteger(
                Value(
                    values,
                    "--duration",
                    "DNF_LOADTEST_DURATION_SECONDS",
                    "10"),
                "duration",
                1,
                MaximumDurationSeconds);
            int timeoutSeconds = ParseInteger(
                Value(
                    values,
                    "--timeout",
                    "DNF_LOADTEST_SESSION_TIMEOUT_SECONDS",
                    "30"),
                "timeout",
                1,
                300);
            int authPort = ParseInteger(
                Value(values, "--auth-port", "DNF_AUTH_PORT", "7443"),
                "auth-port",
                1,
                65535);

            if (concurrency > accountCount)
            {
                throw new ArgumentException(
                    "concurrency cannot be greater than accounts.");
            }

            if (profile == LoadTestProfile.Dungeon &&
                (accountCount % 4 != 0 || concurrency < 4))
            {
                throw new ArgumentException(
                    "dungeon profile requires an account count divisible by 4 " +
                    "and concurrency of at least 4.");
            }

            string accountPrefix = Value(
                values,
                "--account-prefix",
                "DNF_LOADTEST_ACCOUNT_PREFIX",
                "load_user_");
            ValidateAccountPrefix(accountPrefix);

            string password = RequiredEnvironment("DNF_LOADTEST_PASSWORD");
            if (password.Length > 1024)
            {
                throw new ArgumentException(
                    "DNF_LOADTEST_PASSWORD cannot exceed 1024 characters.");
            }

            string fingerprint = RequiredEnvironment(
                "DNF_AUTH_CERT_FINGERPRINT");
            _ = DnfMockClient.Networking.CertificateFingerprint
                .CreateValidation(fingerprint);

            var options = new LoadTestOptions
            {
                Profile = profile,
                AccountCount = accountCount,
                Concurrency = concurrency,
                Duration = TimeSpan.FromSeconds(durationSeconds),
                SessionTimeout = TimeSpan.FromSeconds(timeoutSeconds),
                AuthHost = Value(
                    values,
                    "--auth-host",
                    "DNF_AUTH_HOST",
                    "localhost"),
                AuthPort = authPort,
                CertificateFingerprint = fingerprint,
                AccountPrefix = accountPrefix,
                Password = password,
                FailOnSessionError = failOnSessionError,
                ValidateOnly = validateOnly
            };

            if (string.IsNullOrWhiteSpace(options.AuthHost))
            {
                throw new ArgumentException("auth-host cannot be empty.");
            }

            return ParseResult.Success(options);
        }
        catch (ArgumentException exception)
        {
            return ParseResult.Failure(exception.Message);
        }
    }

    public static void PrintUsage(TextWriter output)
    {
        output.WriteLine("DNF load-test client");
        output.WriteLine();
        output.WriteLine("Usage:");
        output.WriteLine("  dotnet run --project client/loadtest/DNFMockClient.LoadTest.csproj -- [options]");
        output.WriteLine();
        output.WriteLine("Options:");
        output.WriteLine("  --profile lobby|dungeon       Scenario profile (default: lobby)");
        output.WriteLine("  --accounts 1..1000            Unique virtual accounts (default: 100)");
        output.WriteLine("  --concurrency 1..1000         Maximum live sessions (default: 20)");
        output.WriteLine("  --duration 1..3600            Hold time per session in seconds (default: 10)");
        output.WriteLine("  --timeout 1..300              Setup timeout per session/group (default: 30)");
        output.WriteLine("  --auth-host HOST              Authentication server (default: localhost)");
        output.WriteLine("  --auth-port PORT              Authentication TLS port (default: 7443)");
        output.WriteLine("  --account-prefix PREFIX       Provisioned login prefix (default: load_user_)");
        output.WriteLine("  --fail-on-session-error       Return exit code 1 if any session fails");
        output.WriteLine("  --validate-only               Validate configuration without connecting");
        output.WriteLine("  --help                        Show this help");
        output.WriteLine();
        output.WriteLine("Required secret environment variables:");
        output.WriteLine("  DNF_LOADTEST_PASSWORD");
        output.WriteLine("  DNF_AUTH_CERT_FINGERPRINT");
        output.WriteLine();
        output.WriteLine("CLI values override matching non-secret environment variables.");
    }

    private static readonly HashSet<string> KnownValueOptions = new(
        StringComparer.Ordinal)
    {
        "--profile",
        "--accounts",
        "--concurrency",
        "--duration",
        "--timeout",
        "--auth-host",
        "--auth-port",
        "--account-prefix"
    };

    private static string Value(
        IReadOnlyDictionary<string, string> values,
        string option,
        string environmentName,
        string defaultValue)
    {
        if (values.TryGetValue(option, out string? value))
        {
            return value;
        }

        return Environment.GetEnvironmentVariable(environmentName) ??
            defaultValue;
    }

    private static string RequiredEnvironment(string name)
    {
        string? value = Environment.GetEnvironmentVariable(name);
        if (string.IsNullOrEmpty(value))
        {
            throw new ArgumentException(
                $"Required environment variable is missing: {name}");
        }

        return value;
    }

    private static int ParseInteger(
        string text,
        string name,
        int minimum,
        int maximum)
    {
        if (!int.TryParse(
                text,
                NumberStyles.None,
                CultureInfo.InvariantCulture,
                out int value) ||
            value < minimum || value > maximum)
        {
            throw new ArgumentException(
                $"{name} must be between {minimum} and {maximum}.");
        }

        return value;
    }

    private static LoadTestProfile ParseProfile(string value)
    {
        return value.ToLowerInvariant() switch
        {
            "lobby" => LoadTestProfile.Lobby,
            "dungeon" => LoadTestProfile.Dungeon,
            _ => throw new ArgumentException(
                "profile must be either lobby or dungeon.")
        };
    }

    private static bool ReadBooleanEnvironment(
        string name,
        bool defaultValue)
    {
        string? value = Environment.GetEnvironmentVariable(name);
        if (string.IsNullOrEmpty(value))
        {
            return defaultValue;
        }

        if (bool.TryParse(value, out bool parsed))
        {
            return parsed;
        }

        throw new ArgumentException($"{name} must be true or false.");
    }

    private static void ValidateAccountPrefix(string prefix)
    {
        if (prefix.Length is < 1 or > 28 ||
            prefix.Any(character =>
                !char.IsAsciiLetterOrDigit(character) && character != '_'))
        {
            throw new ArgumentException(
                "account-prefix must contain 1..28 ASCII letters, digits, or underscores.");
        }
    }
}

internal sealed record ParseResult(
    LoadTestOptions? Options,
    string? Error,
    bool ShowHelp)
{
    public static ParseResult Success(LoadTestOptions options)
    {
        return new ParseResult(options, null, false);
    }

    public static ParseResult Failure(string error)
    {
        return new ParseResult(null, error, false);
    }

    public static ParseResult Help()
    {
        return new ParseResult(null, null, true);
    }
}
