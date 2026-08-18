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
bool IsValid(const PlayerInputMessage& input)
{
    return input.dungeonId != 0 &&
           std::isfinite(input.moveX) &&
           std::isfinite(input.moveY) &&
           input.moveX >= -1.0f && input.moveX <= 1.0f &&
           input.moveY >= -1.0f && input.moveY <= 1.0f;
}
} // namespace

std::vector<std::uint8_t> EncodePlayerInput(
    const PlayerInputMessage& input)
{
    if (!IsValid(input))
    {
        throw std::invalid_argument("Invalid player input");
    }

    flatbuffers::FlatBufferBuilder builder;

    const auto playerInput = Dnf::Protocol::CreatePlayerInput(
        builder,
        input.sequence,
        input.moveX,
        input.moveY,
        input.jump);

    const auto message = Dnf::Protocol::CreateDungeonMessage(
        builder,
        DUNGEON_PROTOCOL_VERSION,
        input.dungeonId,
        Dnf::Protocol::DungeonPayload_PlayerInput,
        playerInput.Union());

    Dnf::Protocol::FinishDungeonMessageBuffer(builder, message);

    return std::vector<std::uint8_t>(
        builder.GetBufferPointer(),
        builder.GetBufferPointer() + builder.GetSize());
}

bool DecodePlayerInput(
    const std::vector<std::uint8_t>& bytes,
    PlayerInputMessage& output)
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
            Dnf::Protocol::DungeonPayload_PlayerInput)
    {
        return false;
    }

    const Dnf::Protocol::PlayerInput* playerInput =
        message->payload_as_PlayerInput();
    if (playerInput == nullptr)
    {
        return false;
    }

    PlayerInputMessage decoded;
    decoded.dungeonId = message->dungeon_id();
    decoded.sequence = playerInput->sequence();
    decoded.moveX = playerInput->move_x();
    decoded.moveY = playerInput->move_y();
    decoded.jump = playerInput->jump();

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
    const auto snapshot = Dnf::Protocol::CreateDungeonSnapshot(
        builder,
        serverTick,
        playerVector);

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
