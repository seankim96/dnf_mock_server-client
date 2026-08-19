#include "DungeonProtocol.h"

#include "DungeonMessage_generated.h"

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>

#include <cmath>
#include <stdexcept>
#include <vector>

namespace dnf
{
namespace
{
bool IsValid(const PlayerMovementMessage& movement)
{
    return movement.dungeonId != 0 &&
           std::isfinite(movement.moveX) &&
           std::isfinite(movement.moveY) &&
           movement.moveX >= -1.0f && movement.moveX <= 1.0f &&
           movement.moveY >= -1.0f && movement.moveY <= 1.0f;
}

bool IsValid(const PlayerAttackMessage& attack)
{
    const float directionLengthSquared =
        attack.directionX * attack.directionX +
        attack.directionY * attack.directionY;

    return attack.dungeonId != 0 &&
           attack.skillId != 0 &&
           std::isfinite(attack.directionX) &&
           std::isfinite(attack.directionY) &&
           attack.directionX >= -1.0f && attack.directionX <= 1.0f &&
           attack.directionY >= -1.0f && attack.directionY <= 1.0f &&
           directionLengthSquared > 0.0f;
}

bool IsValid(const UdpHelloMessage& hello)
{
    return hello.dungeonId != 0 &&
           hello.sessionId != 0 &&
           hello.token != 0;
}

bool IsValid(const UdpHeartbeatMessage& heartbeat)
{
    return heartbeat.dungeonId != 0 &&
           heartbeat.sessionId != 0;
}
} // namespace

std::vector<std::uint8_t> EncodePlayerMovement(
    const PlayerMovementMessage& movement)
{
    if (!IsValid(movement))
    {
        throw std::invalid_argument("Invalid player movement");
    }

    flatbuffers::FlatBufferBuilder builder;

    const auto playerMovement = Dnf::Protocol::CreatePlayerMovement(
        builder,
        movement.sequence,
        movement.moveX,
        movement.moveY,
        movement.jump);

    const auto message = Dnf::Protocol::CreateDungeonMessage(
        builder,
        DUNGEON_PROTOCOL_VERSION,
        movement.dungeonId,
        Dnf::Protocol::DungeonPayload_PlayerMovement,
        playerMovement.Union());

    Dnf::Protocol::FinishDungeonMessageBuffer(builder, message);

    return std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
}

bool DecodePlayerMovement(
    const std::vector<std::uint8_t>& bytes,
    PlayerMovementMessage& output)
{
    if (bytes.empty())
    {
        return false;
    }

    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!Dnf::Protocol::VerifyDungeonMessageBuffer(verifier))
    {
        return false;
    }

    const Dnf::Protocol::DungeonMessage* message =
        Dnf::Protocol::GetDungeonMessage(bytes.data());

    if (message->protocol_version() != DUNGEON_PROTOCOL_VERSION ||
        message->payload_type() !=
            Dnf::Protocol::DungeonPayload_PlayerMovement)
    {
        return false;
    }

    const Dnf::Protocol::PlayerMovement* playerMovement =
        message->payload_as_PlayerMovement();
    if (playerMovement == nullptr)
    {
        return false;
    }

    PlayerMovementMessage decoded;
    decoded.dungeonId = message->dungeon_id();
    decoded.sequence = playerMovement->sequence();
    decoded.moveX = playerMovement->move_x();
    decoded.moveY = playerMovement->move_y();
    decoded.jump = playerMovement->jump();

    if (!IsValid(decoded))
    {
        return false;
    }

    output = decoded;
    return true;
}

std::vector<std::uint8_t> EncodePlayerAttack(
    const PlayerAttackMessage& attack)
{
    if (!IsValid(attack))
    {
        throw std::invalid_argument("Invalid player attack");
    }

    flatbuffers::FlatBufferBuilder builder;

    const auto playerAttack = Dnf::Protocol::CreatePlayerAttack(
        builder,
        attack.sequence,
        attack.skillId,
        attack.directionX,
        attack.directionY);

    const auto message = Dnf::Protocol::CreateDungeonMessage(
        builder,
        DUNGEON_PROTOCOL_VERSION,
        attack.dungeonId,
        Dnf::Protocol::DungeonPayload_PlayerAttack,
        playerAttack.Union());

    Dnf::Protocol::FinishDungeonMessageBuffer(builder, message);

    return std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
}

bool DecodePlayerAttack(
    const std::vector<std::uint8_t>& bytes,
    PlayerAttackMessage& output)
{
    if (bytes.empty())
    {
        return false;
    }

    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!Dnf::Protocol::VerifyDungeonMessageBuffer(verifier))
    {
        return false;
    }

    const Dnf::Protocol::DungeonMessage* message =
        Dnf::Protocol::GetDungeonMessage(bytes.data());

    if (message->protocol_version() != DUNGEON_PROTOCOL_VERSION ||
        message->payload_type() !=
            Dnf::Protocol::DungeonPayload_PlayerAttack)
    {
        return false;
    }

    const Dnf::Protocol::PlayerAttack* playerAttack =
        message->payload_as_PlayerAttack();
    if (playerAttack == nullptr)
    {
        return false;
    }

    PlayerAttackMessage decoded;
    decoded.dungeonId = message->dungeon_id();
    decoded.sequence = playerAttack->sequence();
    decoded.skillId = playerAttack->skill_id();
    decoded.directionX = playerAttack->direction_x();
    decoded.directionY = playerAttack->direction_y();

    if (!IsValid(decoded))
    {
        return false;
    }

    output = decoded;
    return true;
}

std::vector<std::uint8_t> EncodeUdpHello(
    const UdpHelloMessage& hello)
{
    if (!IsValid(hello))
    {
        throw std::invalid_argument("Invalid UDP hello");
    }

    flatbuffers::FlatBufferBuilder builder;

    const auto udpHello = Dnf::Protocol::CreateUdpHello(
        builder,
        hello.sessionId,
        hello.token);

    const auto message = Dnf::Protocol::CreateDungeonMessage(
        builder,
        DUNGEON_PROTOCOL_VERSION,
        hello.dungeonId,
        Dnf::Protocol::DungeonPayload_UdpHello,
        udpHello.Union());

    Dnf::Protocol::FinishDungeonMessageBuffer(builder, message);

    return std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
}

bool DecodeUdpHello(
    const std::vector<std::uint8_t>& bytes,
    UdpHelloMessage& output)
{
    if (bytes.empty())
    {
        return false;
    }

    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!Dnf::Protocol::VerifyDungeonMessageBuffer(verifier))
    {
        return false;
    }

    const Dnf::Protocol::DungeonMessage* message =
        Dnf::Protocol::GetDungeonMessage(bytes.data());

    if (message->protocol_version() != DUNGEON_PROTOCOL_VERSION ||
        message->payload_type() != Dnf::Protocol::DungeonPayload_UdpHello)
    {
        return false;
    }

    const Dnf::Protocol::UdpHello* udpHello =
        message->payload_as_UdpHello();
    if (udpHello == nullptr)
    {
        return false;
    }

    UdpHelloMessage decoded;
    decoded.dungeonId = message->dungeon_id();
    decoded.sessionId = udpHello->session_id();
    decoded.token = udpHello->token();

    if (!IsValid(decoded))
    {
        return false;
    }

    output = decoded;
    return true;
}

std::vector<std::uint8_t> EncodeUdpHeartbeat(
    const UdpHeartbeatMessage& heartbeat)
{
    if (!IsValid(heartbeat))
    {
        throw std::invalid_argument("Invalid UDP heartbeat");
    }

    flatbuffers::FlatBufferBuilder builder;

    const auto udpHeartbeat = Dnf::Protocol::CreateUdpHeartbeat(
        builder,
        heartbeat.sessionId);

    const auto message = Dnf::Protocol::CreateDungeonMessage(
        builder,
        DUNGEON_PROTOCOL_VERSION,
        heartbeat.dungeonId,
        Dnf::Protocol::DungeonPayload_UdpHeartbeat,
        udpHeartbeat.Union());

    Dnf::Protocol::FinishDungeonMessageBuffer(builder, message);

    return std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
}

bool DecodeUdpHeartbeat(
    const std::vector<std::uint8_t>& bytes,
    UdpHeartbeatMessage& output)
{
    if (bytes.empty())
    {
        return false;
    }

    flatbuffers::Verifier verifier(bytes.data(), bytes.size());
    if (!Dnf::Protocol::VerifyDungeonMessageBuffer(verifier))
    {
        return false;
    }

    const Dnf::Protocol::DungeonMessage* message =
        Dnf::Protocol::GetDungeonMessage(bytes.data());

    if (message->protocol_version() != DUNGEON_PROTOCOL_VERSION ||
        message->payload_type() !=
            Dnf::Protocol::DungeonPayload_UdpHeartbeat)
    {
        return false;
    }

    const Dnf::Protocol::UdpHeartbeat* udpHeartbeat =
        message->payload_as_UdpHeartbeat();
    if (udpHeartbeat == nullptr)
    {
        return false;
    }

    UdpHeartbeatMessage decoded;
    decoded.dungeonId = message->dungeon_id();
    decoded.sessionId = udpHeartbeat->session_id();

    if (!IsValid(decoded))
    {
        return false;
    }

    output = decoded;
    return true;
}

std::vector<std::uint8_t> EncodeDungeonSnapshot(
    const DungeonInstance& dungeon,
    std::uint32_t serverTick)
{
    flatbuffers::FlatBufferBuilder builder;

    std::vector<flatbuffers::Offset<Dnf::Protocol::PlayerSnapshot>> players;
    players.reserve(dungeon.Participants().size());

    for (SessionId sessionId : dungeon.Participants())
    {
        const auto player = dungeon.FindPlayer(sessionId);
        if (player == nullptr)
        {
            throw std::runtime_error("Dungeon player state not found");
        }

        const DungeonPlayerSnapshot snapshot = player->Snapshot();
        const Dnf::Protocol::Vec3 position(
            snapshot.position.x,
            snapshot.position.y,
            snapshot.position.z);

        players.push_back(Dnf::Protocol::CreatePlayerSnapshot(
            builder,
            sessionId,
            snapshot.roomId,
            &position));
    }

    const auto playerVector = builder.CreateVector(players);

    std::vector<flatbuffers::Offset<Dnf::Protocol::EnemySnapshot>> enemies;
    const std::vector<DungeonEnemySnapshot> enemySnapshots =
        dungeon.EnemySnapshots();
    enemies.reserve(enemySnapshots.size());

    for (const DungeonEnemySnapshot& snapshot : enemySnapshots)
    {
        const Dnf::Protocol::Vec3 position(
            snapshot.enemy.position.x,
            snapshot.enemy.position.y,
            snapshot.enemy.position.z);

        enemies.push_back(Dnf::Protocol::CreateEnemySnapshot(
            builder,
            snapshot.enemy.entityId,
            snapshot.enemy.enemyTemplateId,
            snapshot.roomId,
            &position,
            snapshot.enemy.currentHp,
            snapshot.enemy.alive));
    }

    const auto enemyVector = builder.CreateVector(enemies);
    const auto snapshot = Dnf::Protocol::CreateDungeonSnapshot(
        builder,
        serverTick,
        playerVector,
        enemyVector);

    const auto message = Dnf::Protocol::CreateDungeonMessage(
        builder,
        DUNGEON_PROTOCOL_VERSION,
        dungeon.Id(),
        Dnf::Protocol::DungeonPayload_DungeonSnapshot,
        snapshot.Union());

    Dnf::Protocol::FinishDungeonMessageBuffer(builder, message);

    return std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
}
} // namespace dnf
