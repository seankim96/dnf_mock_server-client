using System;

namespace DnfMockClient;

public readonly record struct DungeonDirection(float X, float Y);

public readonly record struct DungeonMovementCommand(
    float DirectionX,
    float DirectionY,
    bool Jump);

public readonly record struct DungeonAttackCommand(
    uint SkillId,
    float DirectionX,
    float DirectionY);

public readonly record struct DungeonInputCommands(
    DungeonMovementCommand? Movement,
    DungeonAttackCommand? Attack);

public sealed class DungeonInputController
{
    public const uint DefaultSkillId = 1001;
    public const double MovementSendIntervalSeconds = 0.05;

    private double _movementSendTime;
    private bool _movementSendInProgress;
    private bool _attackPressed;
    private DungeonDirection _lastDirection = new(1.0f, 0.0f);

    public DungeonDirection LastDirection => _lastDirection;

    public DungeonInputCommands Advance(
        double deltaSeconds,
        DungeonDirection direction,
        bool jump,
        bool attackPressed)
    {
        if (!double.IsFinite(deltaSeconds) || deltaSeconds < 0.0)
        {
            throw new ArgumentOutOfRangeException(nameof(deltaSeconds));
        }

        direction = Normalize(direction);
        RememberDirection(direction);

        _movementSendTime += deltaSeconds;
        DungeonMovementCommand? movement = null;

        if (_movementSendTime >= MovementSendIntervalSeconds &&
            !_movementSendInProgress)
        {
            _movementSendTime = 0.0;
            _movementSendInProgress = true;
            movement = new DungeonMovementCommand(
                direction.X,
                direction.Y,
                jump);
        }

        DungeonAttackCommand? attack = CreateAttackOnPress(attackPressed);
        _attackPressed = attackPressed;

        return new DungeonInputCommands(movement, attack);
    }

    public DungeonMovementCommand? PressDirection(DungeonDirection direction)
    {
        direction = Normalize(direction);
        if (IsZero(direction))
        {
            return null;
        }

        RememberDirection(direction);

        if (_movementSendInProgress)
        {
            return null;
        }

        _movementSendInProgress = true;
        return new DungeonMovementCommand(direction.X, direction.Y, false);
    }

    public DungeonAttackCommand? PressAttack()
    {
        DungeonAttackCommand? attack = CreateAttackOnPress(true);
        _attackPressed = true;
        return attack;
    }

    public void CompleteMovementSend()
    {
        _movementSendInProgress = false;
    }

    private DungeonAttackCommand? CreateAttackOnPress(bool attackPressed)
    {
        if (!attackPressed || _attackPressed)
        {
            return null;
        }

        return new DungeonAttackCommand(
            DefaultSkillId,
            _lastDirection.X,
            _lastDirection.Y);
    }

    private void RememberDirection(DungeonDirection direction)
    {
        if (!IsZero(direction))
        {
            _lastDirection = direction;
        }
    }

    private static DungeonDirection Normalize(DungeonDirection direction)
    {
        if (!float.IsFinite(direction.X) || !float.IsFinite(direction.Y))
        {
            throw new ArgumentOutOfRangeException(nameof(direction));
        }

        float lengthSquared =
            direction.X * direction.X + direction.Y * direction.Y;
        if (lengthSquared <= 1.0f)
        {
            return direction;
        }

        float length = MathF.Sqrt(lengthSquared);
        return new DungeonDirection(
            direction.X / length,
            direction.Y / length);
    }

    private static bool IsZero(DungeonDirection direction)
    {
        return direction.X == 0.0f && direction.Y == 0.0f;
    }
}
