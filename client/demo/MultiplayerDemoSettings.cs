namespace DnfMockClient.MultiplayerDemo;

internal sealed class MultiplayerDemoSettings
{
    public required string AuthHost { get; init; }
    public required int AuthPort { get; init; }
    public required string CertificateFingerprint { get; init; }
    public required string AccountPrefix { get; init; }
    public required string Password { get; init; }
    public required int ClientCount { get; init; }

    public static MultiplayerDemoSettings FromEnvironment()
    {
        int authPort = ReadInt("DNF_AUTH_PORT", 1, 65535);
        int clientCount = ReadInt("DNF_DEMO_CLIENT_COUNT", 2, 4);

        return new MultiplayerDemoSettings
        {
            AuthHost = ReadRequired("DNF_AUTH_HOST"),
            AuthPort = authPort,
            CertificateFingerprint =
                ReadRequired("DNF_AUTH_CERT_FINGERPRINT"),
            AccountPrefix = ReadRequired("DNF_DEMO_ACCOUNT_PREFIX"),
            Password = ReadRequired("DNF_AUTH_PASSWORD"),
            ClientCount = clientCount
        };
    }

    public string LoginId(int index)
    {
        return $"{AccountPrefix}{index + 1}";
    }

    private static string ReadRequired(string name)
    {
        string? value = Environment.GetEnvironmentVariable(name);
        if (string.IsNullOrWhiteSpace(value))
        {
            throw new InvalidOperationException(
                $"Required environment variable is missing: {name}");
        }

        return value;
    }

    private static int ReadInt(string name, int minimum, int maximum)
    {
        string value = ReadRequired(name);
        if (!int.TryParse(value, out int parsed) ||
            parsed < minimum || parsed > maximum)
        {
            throw new InvalidOperationException(
                $"{name} must be between {minimum} and {maximum}.");
        }

        return parsed;
    }
}
