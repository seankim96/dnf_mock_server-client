using Xunit;

namespace DnfMockClient.Tests;

[CollectionDefinition(Name, DisableParallelization = true)]
public sealed class SocketTestCollection
{
    public const string Name = "Socket integration tests";
}
