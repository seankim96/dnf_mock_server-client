using DnfMockClient;

internal static class DungeonInputControllerSmokeTests
{
    public static void Run()
    {
        SendsMovementAtTheConfiguredCadence();
        NormalizesDiagonalMovement();
        AllowsOnlyOneMovementSendAtATime();
        UsesTheLatestDirectionForAttackEdges();
        RejectsInvalidDeltaTime();
    }

    private static void SendsMovementAtTheConfiguredCadence()
    {
        var controller = new DungeonInputController();

        DungeonInputCommands early = controller.Advance(
            0.04,
            new DungeonDirection(1.0f, 0.0f),
            false,
            false);
        Check(early.Movement is null,
            "Movement was sent before the cadence elapsed.");

        DungeonInputCommands due = controller.Advance(
            0.02,
            new DungeonDirection(1.0f, 0.0f),
            true,
            false);
        Check(due.Movement is { } movement &&
            Near(movement.DirectionX, 1.0f) &&
            Near(movement.DirectionY, 0.0f) && movement.Jump,
            "Movement was not sent when the cadence elapsed.");
    }

    private static void NormalizesDiagonalMovement()
    {
        var controller = new DungeonInputController();

        DungeonInputCommands commands = controller.Advance(
            DungeonInputController.MovementSendIntervalSeconds,
            new DungeonDirection(1.0f, 1.0f),
            false,
            false);

        Check(commands.Movement is { } movement &&
            Near(movement.DirectionX, MathF.Sqrt(0.5f)) &&
            Near(movement.DirectionY, MathF.Sqrt(0.5f)),
            "Diagonal movement was not normalized.");
    }

    private static void AllowsOnlyOneMovementSendAtATime()
    {
        var controller = new DungeonInputController();

        DungeonMovementCommand? first = controller.PressDirection(
            new DungeonDirection(-1.0f, 0.0f));
        DungeonMovementCommand? blocked = controller.PressDirection(
            new DungeonDirection(0.0f, -1.0f));
        DungeonInputCommands overdue = controller.Advance(
            DungeonInputController.MovementSendIntervalSeconds,
            new DungeonDirection(0.0f, -1.0f),
            false,
            false);

        Check(first is not null, "The first movement send was rejected.");
        Check(blocked is null && overdue.Movement is null,
            "A second movement send started while one was in progress.");

        controller.CompleteMovementSend();
        DungeonInputCommands resumed = controller.Advance(
            0.0,
            new DungeonDirection(0.0f, -1.0f),
            false,
            false);
        Check(resumed.Movement is not null,
            "An overdue movement send did not resume after completion.");
    }

    private static void UsesTheLatestDirectionForAttackEdges()
    {
        var controller = new DungeonInputController();
        controller.PressDirection(new DungeonDirection(-1.0f, 0.0f));

        DungeonAttackCommand? first = controller.PressAttack();
        DungeonAttackCommand? held = controller.PressAttack();

        Check(first is { } attack &&
            attack.SkillId == DungeonInputController.DefaultSkillId &&
            Near(attack.DirectionX, -1.0f) && Near(attack.DirectionY, 0.0f),
            "Attack did not use the latest movement direction.");
        Check(held is null, "A held attack key emitted another attack.");

        controller.Advance(0.0, default, false, false);
        Check(controller.PressAttack() is not null,
            "A new attack edge was not accepted after key release.");
    }

    private static void RejectsInvalidDeltaTime()
    {
        var controller = new DungeonInputController();

        try
        {
            controller.Advance(-0.001, default, false, false);
        }
        catch (ArgumentOutOfRangeException)
        {
            return;
        }

        throw new InvalidOperationException("Negative delta time was accepted.");
    }

    private static bool Near(float left, float right)
    {
        return MathF.Abs(left - right) < 0.001f;
    }

    private static void Check(bool condition, string message)
    {
        if (!condition)
        {
            throw new InvalidOperationException(message);
        }
    }
}
