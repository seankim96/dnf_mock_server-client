namespace DnfMockClient.Networking;

public sealed class ServerTickTracker
{
    private const uint HalfRange = 1U << 31;

    private readonly object _lock = new();
    private bool _hasAcceptedTick;
    private uint _lastAcceptedTick;

    public bool TryAccept(uint serverTick)
    {
        lock (_lock)
        {
            if (!_hasAcceptedTick)
            {
                _hasAcceptedTick = true;
                _lastAcceptedTick = serverTick;
                return true;
            }

            uint forwardDistance = unchecked(serverTick - _lastAcceptedTick);
            if (forwardDistance == 0 || forwardDistance >= HalfRange)
            {
                return false;
            }

            _lastAcceptedTick = serverTick;
            return true;
        }
    }

    public void Reset()
    {
        lock (_lock)
        {
            _hasAcceptedTick = false;
            _lastAcceptedTick = 0;
        }
    }
}
