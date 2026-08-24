using DnfMockClient.Networking;
using Xunit;

namespace DnfMockClient.Tests;

public sealed class ServerTickTrackerTests
{
    [Fact]
    public void AcceptsTheFirstTick()
    {
        var tracker = new ServerTickTracker();

        Assert(tracker.TryAccept(0), "The first tick must be accepted.");
    }

    [Fact]
    public void RejectsDuplicateAndOlderTicks()
    {
        var tracker = new ServerTickTracker();

        Assert(tracker.TryAccept(100), "The first tick must be accepted.");
        Assert(!tracker.TryAccept(100), "A duplicate tick was accepted.");
        Assert(!tracker.TryAccept(99), "An older tick was accepted.");
        Assert(tracker.TryAccept(101), "A newer tick was rejected.");
    }

    [Fact]
    public void AcceptsTicksAcrossWrapAround()
    {
        var tracker = new ServerTickTracker();

        Assert(tracker.TryAccept(uint.MaxValue - 1),
            "The initial pre-wrap tick was rejected.");
        Assert(tracker.TryAccept(uint.MaxValue),
            "The final pre-wrap tick was rejected.");
        Assert(tracker.TryAccept(0), "The wrapped tick was rejected.");
        Assert(tracker.TryAccept(1), "The post-wrap tick was rejected.");
    }

    [Fact]
    public void RejectsLatePacketsAfterWrapAround()
    {
        var tracker = new ServerTickTracker();

        Assert(tracker.TryAccept(uint.MaxValue),
            "The initial pre-wrap tick was rejected.");
        Assert(tracker.TryAccept(0), "The wrapped tick was rejected.");
        Assert(!tracker.TryAccept(uint.MaxValue),
            "A late pre-wrap tick was accepted.");
    }

    [Fact]
    public void RejectsAnAmbiguousHalfRangeTick()
    {
        var tracker = new ServerTickTracker();

        Assert(tracker.TryAccept(7), "The first tick must be accepted.");
        Assert(!tracker.TryAccept(7 + (1U << 31)),
            "A tick exactly half a sequence range away was accepted.");
    }

    [Fact]
    public void ResetStartsANewSequence()
    {
        var tracker = new ServerTickTracker();

        Assert(tracker.TryAccept(500), "The first tick must be accepted.");
        tracker.Reset();
        Assert(tracker.TryAccept(10),
            "The first tick of a new session was rejected.");
    }

    private static void Assert(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
