using Xunit.Sdk;

namespace DnfMockClient.Tests;

internal static class TestAssertions
{
    internal static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new XunitException(message);
        }
    }

    internal static void AssertThrows<TException>(
        Action action,
        string message)
        where TException : Exception
    {
        try
        {
            action();
        }
        catch (TException)
        {
            return;
        }
        catch (Exception exception)
        {
            throw new XunitException(
                $"{message} Expected {typeof(TException).Name}, " +
                $"but received {exception.GetType().Name}.");
        }

        throw new XunitException(
            $"{message} Expected {typeof(TException).Name}.");
    }
}
