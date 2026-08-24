using DnfMockClient.Protocol;
using Godot;

namespace DnfMockClient;

public partial class DungeonWorldView : Control
{
    private readonly DungeonSnapshotBuffer _snapshots = new(30.0);
    private DungeonStaticData? _staticData;
    private ulong _localSessionId;

    public void SetLocalSessionId(ulong sessionId)
    {
        _localSessionId = sessionId;
        QueueRedraw();
    }

    public void SetSnapshot(DungeonSnapshotData snapshot)
    {
        _snapshots.Push(snapshot);
        QueueRedraw();
    }

    public void SetStaticData(DungeonStaticData staticData)
    {
        _staticData = staticData;
        QueueRedraw();
    }

    public void ClearStaticData()
    {
        _staticData = null;
        _snapshots.Reset();
        QueueRedraw();
    }

    public override void _Process(double delta)
    {
        if (_snapshots.Advance(delta))
        {
            QueueRedraw();
        }
    }

    public override void _Draw()
    {
        DrawRect(new Rect2(Vector2.Zero, Size), new Color("121725"));

        if (_staticData is null)
        {
            DrawCenteredText("TCP 정적 데이터를 기다리는 중...");
            return;
        }

        DungeonSnapshotData? snapshot = _snapshots.Current;
        if (snapshot is null)
        {
            DrawCenteredText("UDP 스냅샷을 기다리는 중...");
            return;
        }

        uint roomId = FindCurrentRoomId(snapshot);
        RoomStaticData? room = FindRoom(roomId);
        if (room is null)
        {
            DrawCenteredText($"Room {roomId} 정적 데이터가 없습니다.");
            return;
        }

        var roomSize = new Vector2(room.Width, room.Depth);
        var arena = new Rect2(
            new Vector2(30.0f, 36.0f),
            Size - new Vector2(60.0f, 72.0f));

        DrawRect(arena, new Color("202b3a"), true);
        DrawRect(arena, new Color("52627a"), false, 2.0f);
        DrawGrid(arena);
        DrawStaticGeometry(room, roomSize, arena);

        foreach (EnemySnapshotData enemy in snapshot.Enemies)
        {
            if (enemy.RoomId != roomId || !enemy.Alive)
            {
                continue;
            }

            SnapshotPosition sampled = _snapshots.SampleEnemy(enemy);
            Vector2 position = ToScreen(
                sampled.X,
                sampled.Y,
                roomSize,
                arena);
            var body = new Rect2(position - new Vector2(13.0f, 18.0f),
                new Vector2(26.0f, 36.0f));
            DrawRect(body, new Color("dc5b64"), true);
            DrawRect(body, new Color("ff9b9f"), false, 2.0f);

            uint maxHp = FindEnemyMaxHp(enemy.EnemyTemplateId);
            float hpRatio = Mathf.Clamp(
                enemy.CurrentHp / (float)maxHp,
                0.0f,
                1.0f);
            var hpBackground = new Rect2(position + new Vector2(-18.0f, -28.0f),
                new Vector2(36.0f, 5.0f));
            DrawRect(hpBackground, new Color("3c1f25"), true);
            DrawRect(new Rect2(hpBackground.Position,
                new Vector2(hpBackground.Size.X * hpRatio, hpBackground.Size.Y)),
                new Color("ff5964"), true);
        }

        foreach (PlayerSnapshotData player in snapshot.Players)
        {
            if (player.RoomId != roomId)
            {
                continue;
            }

            bool isLocal = player.SessionId == _localSessionId;
            SnapshotPosition sampled = isLocal
                ? new SnapshotPosition(player.X, player.Y, player.Z)
                : _snapshots.SamplePlayer(player);
            Vector2 position = ToScreen(
                sampled.X,
                sampled.Y,
                roomSize,
                arena);
            Color color = !player.Alive
                ? new Color("6b7280")
                : isLocal
                    ? new Color("58d68d")
                    : new Color("5dade2");
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

            float hpRatio = Mathf.Clamp(
                player.CurrentHp / (float)player.MaxHp,
                0.0f,
                1.0f);
            var hpBackground = new Rect2(
                position + new Vector2(-20.0f, 20.0f),
                new Vector2(40.0f, 5.0f));
            DrawRect(hpBackground, new Color("23332a"), true);
            DrawRect(
                new Rect2(
                    hpBackground.Position,
                    new Vector2(
                        hpBackground.Size.X * hpRatio,
                        hpBackground.Size.Y)),
                new Color("58d68d"),
                true);

            if (player.SkillPhase == SkillPhaseData.Active)
            {
                DrawArc(
                    position,
                    28.0f,
                    0.0f,
                    Mathf.Tau,
                    32,
                    new Color("7dd3fc"),
                    4.0f);
            }
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

    private void DrawStaticGeometry(
        RoomStaticData room,
        Vector2 roomSize,
        Rect2 arena)
    {
        foreach (PortalStaticData portal in room.Portals)
        {
            Rect2 portalRect = ToScreenRect(
                portal.TriggerArea,
                roomSize,
                arena);
            DrawRect(portalRect, new Color(0.20f, 0.75f, 0.85f, 0.18f), true);
            DrawRect(portalRect, new Color("4fd1e1"), false, 2.0f);
        }

        foreach (ObstacleStaticData obstacle in room.Obstacles)
        {
            Rect2 obstacleRect = ToScreenRect(
                obstacle.Collision,
                roomSize,
                arena);
            Color color = obstacle.Destructible
                ? new Color("a87345")
                : new Color("667085");
            DrawRect(obstacleRect, color, true);
            DrawRect(obstacleRect, color.Lightened(0.25f), false, 2.0f);
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

    private RoomStaticData? FindRoom(uint roomId)
    {
        if (_staticData is null)
        {
            return null;
        }

        foreach (RoomStaticData room in _staticData.Rooms)
        {
            if (room.RoomId == roomId)
            {
                return room;
            }
        }

        return null;
    }

    private uint FindEnemyMaxHp(uint enemyTemplateId)
    {
        if (_staticData is not null)
        {
            foreach (EnemyTemplateStaticData enemy in _staticData.EnemyTemplates)
            {
                if (enemy.EnemyTemplateId == enemyTemplateId)
                {
                    return enemy.MaxHp;
                }
            }
        }

        return 1;
    }

    private static Rect2 ToScreenRect(
        StaticCollisionBoxData collision,
        Vector2 roomSize,
        Rect2 arena)
    {
        Vector2 minimum = ToScreen(
            collision.Minimum.X,
            collision.Minimum.Y,
            roomSize,
            arena);
        Vector2 maximum = ToScreen(
            collision.Maximum.X,
            collision.Maximum.Y,
            roomSize,
            arena);
        return new Rect2(minimum, maximum - minimum);
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
