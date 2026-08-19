#include "TcpMessage_generated.h"

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>

#include <cassert>
#include <cstdint>
#include <iostream>

namespace protocol = Dnf::Protocol::Tcp;

namespace
{
void TestCurrentLoginPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto playerName = builder.CreateString("Player_1");
    const auto login = protocol::CreateLoginRequest(builder, playerName);
    const auto message = protocol::CreateTcpMessage(
        builder,
        1,
        protocol::TcpPayload_LoginRequest,
        login.Union());
    protocol::FinishTcpMessageBuffer(builder, message);

    flatbuffers::Verifier verifier(
        builder.GetBufferPointer(),
        builder.GetSize());
    assert(protocol::VerifyTcpMessageBuffer(verifier));

    const auto* decoded =
        protocol::GetTcpMessage(builder.GetBufferPointer());
    assert(decoded->protocol_version() == 1);
    assert(decoded->payload_type() == protocol::TcpPayload_LoginRequest);
    assert(decoded->payload_as_LoginRequest() != nullptr);
    assert(decoded->payload_as_LoginRequest()->player_name()->str() ==
           "Player_1");
}

void TestCurrentPartySnapshotPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const std::uint64_t members[] = {42, 43};
    const auto memberIds = builder.CreateVector(members, 2);
    const auto snapshot = protocol::CreatePartySnapshotResponse(
        builder,
        protocol::PartySnapshotResult_Success,
        9,
        42,
        memberIds);
    const auto message = protocol::CreateTcpMessage(
        builder,
        1,
        protocol::TcpPayload_PartySnapshotResponse,
        snapshot.Union());
    protocol::FinishTcpMessageBuffer(builder, message);

    flatbuffers::Verifier verifier(
        builder.GetBufferPointer(),
        builder.GetSize());
    assert(protocol::VerifyTcpMessageBuffer(verifier));

    const auto* decoded =
        protocol::GetTcpMessage(builder.GetBufferPointer());
    const auto* party = decoded->payload_as_PartySnapshotResponse();
    assert(party != nullptr);
    assert(party->party_id() == 9);
    assert(party->leader_session_id() == 42);
    assert(party->member_session_ids()->size() == 2);
    assert(party->member_session_ids()->Get(1) == 43);
}

void TestPlannedCatalogPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto displayName = builder.CreateString("Forest");
    const auto forest = protocol::CreateDungeonCatalogEntry(
        builder,
        1001,
        displayName,
        1,
        4,
        true);
    const auto dungeons = builder.CreateVector(&forest, 1);
    const auto catalog = protocol::CreateDungeonCatalogResponse(
        builder,
        protocol::CatalogResult_Success,
        dungeons);
    const auto message = protocol::CreateTcpMessage(
        builder,
        1,
        protocol::TcpPayload_DungeonCatalogResponse,
        catalog.Union());
    protocol::FinishTcpMessageBuffer(builder, message);

    flatbuffers::Verifier verifier(
        builder.GetBufferPointer(),
        builder.GetSize());
    assert(protocol::VerifyTcpMessageBuffer(verifier));

    const auto* decoded =
        protocol::GetTcpMessage(builder.GetBufferPointer());
    const auto* response = decoded->payload_as_DungeonCatalogResponse();
    assert(response != nullptr);
    assert(response->dungeons()->size() == 1);
    assert(response->dungeons()->Get(0)->dungeon_template_id() == 1001);
    assert(response->dungeons()->Get(0)->display_name()->str() == "Forest");
}
} // namespace

int main()
{
    TestCurrentLoginPayload();
    TestCurrentPartySnapshotPayload();
    TestPlannedCatalogPayload();

    std::cout << "All TCP FlatBuffers schema tests passed.\n";
    return 0;
}
