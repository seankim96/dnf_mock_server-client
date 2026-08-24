using System;
using DnfMockClient.Protocol;

namespace DnfMockClient;

public readonly record struct SnapshotPosition(float X, float Y, float Z);

public sealed class DungeonSnapshotBuffer
{
    private const double MaxInterpolationSeconds = 0.25;

    private readonly double _ticksPerSecond;
    private DungeonSnapshotData? _previous;
    private DungeonSnapshotData? _current;
    private double _elapsedSeconds;

    public DungeonSnapshotBuffer(double ticksPerSecond)
    {
        if (!double.IsFinite(ticksPerSecond) || ticksPerSecond <= 0.0)
        {
            throw new ArgumentOutOfRangeException(nameof(ticksPerSecond));
        }

        _ticksPerSecond = ticksPerSecond;
    }

    public DungeonSnapshotData? Current => _current;

    public void Push(DungeonSnapshotData snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        if (_current is not null && _current.DungeonId == snapshot.DungeonId)
        {
            _previous = _current;
        }
        else
        {
            _previous = null;
        }

        _current = snapshot;
        _elapsedSeconds = 0.0;
    }

    public bool Advance(double deltaSeconds)
    {
        if (!double.IsFinite(deltaSeconds) || deltaSeconds < 0.0)
        {
            throw new ArgumentOutOfRangeException(nameof(deltaSeconds));
        }

        double duration = InterpolationDuration();
        if (duration <= 0.0 || _elapsedSeconds >= duration)
        {
            return false;
        }

        _elapsedSeconds = Math.Min(_elapsedSeconds + deltaSeconds, duration);
        return true;
    }

    public SnapshotPosition SamplePlayer(PlayerSnapshotData player)
    {
        ArgumentNullException.ThrowIfNull(player);

        if (_previous is not null)
        {
            foreach (PlayerSnapshotData previous in _previous.Players)
            {
                if (previous.SessionId == player.SessionId &&
                    previous.RoomId == player.RoomId)
                {
                    return Interpolate(
                        previous.X,
                        previous.Y,
                        previous.Z,
                        player.X,
                        player.Y,
                        player.Z);
                }
            }
        }

        return new SnapshotPosition(player.X, player.Y, player.Z);
    }

    public SnapshotPosition SampleEnemy(EnemySnapshotData enemy)
    {
        ArgumentNullException.ThrowIfNull(enemy);

        if (_previous is not null)
        {
            foreach (EnemySnapshotData previous in _previous.Enemies)
            {
                if (previous.EntityId == enemy.EntityId &&
                    previous.RoomId == enemy.RoomId)
                {
                    return Interpolate(
                        previous.X,
                        previous.Y,
                        previous.Z,
                        enemy.X,
                        enemy.Y,
                        enemy.Z);
                }
            }
        }

        return new SnapshotPosition(enemy.X, enemy.Y, enemy.Z);
    }

    public void Reset()
    {
        _previous = null;
        _current = null;
        _elapsedSeconds = 0.0;
    }

    private SnapshotPosition Interpolate(
        float fromX,
        float fromY,
        float fromZ,
        float toX,
        float toY,
        float toZ)
    {
        float amount = (float)InterpolationAmount();
        return new SnapshotPosition(
            fromX + (toX - fromX) * amount,
            fromY + (toY - fromY) * amount,
            fromZ + (toZ - fromZ) * amount);
    }

    private double InterpolationAmount()
    {
        double duration = InterpolationDuration();
        return duration <= 0.0
            ? 1.0
            : Math.Clamp(_elapsedSeconds / duration, 0.0, 1.0);
    }

    private double InterpolationDuration()
    {
        if (_previous is null || _current is null)
        {
            return 0.0;
        }

        uint tickDistance = unchecked(_current.ServerTick - _previous.ServerTick);
        if (tickDistance == 0 || tickDistance >= 0x80000000u)
        {
            return 0.0;
        }

        return Math.Min(tickDistance / _ticksPerSecond, MaxInterpolationSeconds);
    }
}
