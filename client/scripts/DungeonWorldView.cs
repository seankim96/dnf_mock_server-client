using DnfMockClient.Protocol;
using Godot;

namespace DnfMockClient;

public partial class DungeonWorldView : Control
{
    private DungeonSnapshotData? _snapshot;
    private ulong _localSessionId;

    public void SetLocalSessionId(ulong sessionId)
    {
        _localSessionId = sessionId;
        QueueRedraw();
    }

    public void SetSnapshot(DungeonSnapshotData snapshot)
    {
        _snapshot = snapshot;
        QueueRedraw();
    }

    public override void _Draw()
    {
        DrawRect(new Rect2(Vector2.Zero, Size), new Color("121725"));

        if (_snapshot is null)
        {
            DrawCenteredText("UDP 스냅샷을 기다리는 중...");
            return;
        }

        uint roomId = FindCurrentRoomId(_snapshot);
        Vector2 roomSize = roomId == 2
            ? new Vector2(1500.0f, 600.0f)
            : new Vector2(1200.0f, 500.0f);
        var arena = new Rect2(
            new Vector2(30.0f, 36.0f),
            Size - new Vector2(60.0f, 72.0f));

        DrawRect(arena, new Color("202b3a"), true);
        DrawRect(arena, new Color("52627a"), false, 2.0f);
        DrawGrid(arena);

        foreach (EnemySnapshotData enemy in _snapshot.Enemies)
        {
            if (enemy.RoomId != roomId || !enemy.Alive)
            {
                continue;
            }

            Vector2 position = ToScreen(enemy.X, enemy.Y, roomSize, arena);
            var body = new Rect2(position - new Vector2(13.0f, 18.0f),
                new Vector2(26.0f, 36.0f));
            DrawRect(body, new Color("dc5b64"), true);
            DrawRect(body, new Color("ff9b9f"), false, 2.0f);

            float hpRatio = Mathf.Clamp(enemy.CurrentHp / 100.0f, 0.0f, 1.0f);
            var hpBackground = new Rect2(position + new Vector2(-18.0f, -28.0f),
                new Vector2(36.0f, 5.0f));
            DrawRect(hpBackground, new Color("3c1f25"), true);
            DrawRect(new Rect2(hpBackground.Position,
                new Vector2(hpBackground.Size.X * hpRatio, hpBackground.Size.Y)),
                new Color("ff5964"), true);
        }

        foreach (PlayerSnapshotData player in _snapshot.Players)
        {
            if (player.RoomId != roomId)
            {
                continue;
            }

            Vector2 position = ToScreen(player.X, player.Y, roomSize, arena);
            bool isLocal = player.SessionId == _localSessionId;
            Color color = isLocal ? new Color("58d68d") : new Color("5dade2");
            DrawCircle(position, isLocal ? 14.0f : 11.0f, color);
            DrawCircle(position, isLocal ? 14.0f : 11.0f,
                new Color("e8f1ff"), false, 2.0f);
            DrawString(
                ThemeDB.FallbackFont,
                position + new Vector2(-18.0f, -20.0f),
                $"P{player.SessionId}",
                HorizontalAlignment.Left,
                -1,
                12,
                Colors.White);
        }
    }

    private void DrawGrid(Rect2 arena)
    {
        const int columnCount = 12;
        const int rowCount = 5;
        Color gridColor = new Color(0.35f, 0.42f, 0.52f, 0.22f);

        for (int column = 1; column < columnCount; column++)
        {
            float x = arena.Position.X + arena.Size.X * column / columnCount;
            DrawLine(new Vector2(x, arena.Position.Y),
                new Vector2(x, arena.End.Y), gridColor);
        }

        for (int row = 1; row < rowCount; row++)
        {
            float y = arena.Position.Y + arena.Size.Y * row / rowCount;
            DrawLine(new Vector2(arena.Position.X, y),
                new Vector2(arena.End.X, y), gridColor);
        }
    }

    private void DrawCenteredText(string text)
    {
        Vector2 textSize = ThemeDB.FallbackFont.GetStringSize(
            text,
            HorizontalAlignment.Left,
            -1,
            18);
        DrawString(
            ThemeDB.FallbackFont,
            (Size - textSize) * 0.5f,
            text,
            HorizontalAlignment.Left,
            -1,
            18,
            new Color("9aa8bd"));
    }

    private uint FindCurrentRoomId(DungeonSnapshotData snapshot)
    {
        foreach (PlayerSnapshotData player in snapshot.Players)
        {
            if (player.SessionId == _localSessionId)
            {
                return player.RoomId;
            }
        }

        return snapshot.Players.Count > 0 ? snapshot.Players[0].RoomId : 1;
    }

    private static Vector2 ToScreen(
        float x,
        float y,
        Vector2 roomSize,
        Rect2 arena)
    {
        float normalizedX = Mathf.Clamp(x / roomSize.X, 0.0f, 1.0f);
        float normalizedY = Mathf.Clamp(y / roomSize.Y, 0.0f, 1.0f);
        return arena.Position + new Vector2(
            normalizedX * arena.Size.X,
            normalizedY * arena.Size.Y);
    }
}
