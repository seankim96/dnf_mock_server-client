#include "AuthMessage_generated.h"

#include <flatbuffers/flatbuffer_builder.h>
#include <flatbuffers/verifier.h>

#include <cassert>
#include <cstdint>
#include <iostream>

namespace protocol = Dnf::Protocol::Auth;

namespace
{
void TestLoginRequest()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto loginId = builder.CreateString("account_1");
    const auto password = builder.CreateString("test-password");
    const auto request =
        protocol::CreateLoginRequest(builder, loginId, password);
    const auto message = protocol::CreateAuthMessage(
        builder,
        1,
        protocol::AuthPayload_LoginRequest,
        request.Union());
    protocol::FinishAuthMessageBuffer(builder, message);

    flatbuffers::Verifier verifier(
        builder.GetBufferPointer(),
        builder.GetSize());
    assert(protocol::VerifyAuthMessageBuffer(verifier));

    const auto* decoded =
        protocol::GetAuthMessage(builder.GetBufferPointer());
    assert(decoded->protocol_version() == 1);
    assert(decoded->payload_type() ==
           protocol::AuthPayload_LoginRequest);
    assert(decoded->payload_as_LoginRequest()->login_id()->str() ==
           "account_1");
    assert(decoded->payload_as_LoginRequest()->password()->str() ==
           "test-password");
}

void TestCharacterListResponse()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto firstName = builder.CreateString("Player_1");
    const auto secondName = builder.CreateString("Player_2");
    const auto first = protocol::CreateCharacterSummary(
        builder,
        10,
        firstName,
        15);
    const auto second = protocol::CreateCharacterSummary(
        builder,
        20,
        secondName,
        30);
    const flatbuffers::Offset<protocol::CharacterSummary> entries[] = {
        first,
        second};
    const auto characters = builder.CreateVector(entries, 2);
    const auto response = protocol::CreateCharacterListResponse(
        builder,
        protocol::CharacterListResult_Success,
        characters);
    const auto message = protocol::CreateAuthMessage(
        builder,
        1,
        protocol::AuthPayload_CharacterListResponse,
        response.Union());
    protocol::FinishAuthMessageBuffer(builder, message);

    flatbuffers::Verifier verifier(
        builder.GetBufferPointer(),
        builder.GetSize());
    assert(protocol::VerifyAuthMessageBuffer(verifier));

    const auto* decoded =
        protocol::GetAuthMessage(builder.GetBufferPointer());
    const auto* characterList =
        decoded->payload_as_CharacterListResponse();
    assert(characterList != nullptr);
    assert(characterList->characters()->size() == 2);
    assert(characterList->characters()->Get(0)->player_id() == 10);
    assert(characterList->characters()->Get(1)->display_name()->str() ==
           "Player_2");
    assert(characterList->characters()->Get(1)->level() == 30);
}

void TestCharacterSelectionResponse()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto host = builder.CreateString("127.0.0.1");
    const auto ticket = builder.CreateString("one-time-ticket");
    const auto response = protocol::CreateCharacterSelectionResponse(
        builder,
        protocol::CharacterSelectionResult_Success,
        host,
        7777,
        ticket,
        123456789);
    const auto message = protocol::CreateAuthMessage(
        builder,
        1,
        protocol::AuthPayload_CharacterSelectionResponse,
        response.Union());
    protocol::FinishAuthMessageBuffer(builder, message);

    flatbuffers::Verifier verifier(
        builder.GetBufferPointer(),
        builder.GetSize());
    assert(protocol::VerifyAuthMessageBuffer(verifier));

    const auto* decoded =
        protocol::GetAuthMessage(builder.GetBufferPointer());
    const auto* selection =
        decoded->payload_as_CharacterSelectionResponse();
    assert(selection != nullptr);
    assert(selection->game_server_host()->str() == "127.0.0.1");
    assert(selection->game_server_port() == 7777);
    assert(selection->auth_ticket()->str() == "one-time-ticket");
    assert(selection->expires_at_unix() == 123456789);
}
} // namespace

int main()
{
    TestLoginRequest();
    TestCharacterListResponse();
    TestCharacterSelectionResponse();

    std::cout << "All auth FlatBuffers schema tests passed.\n";
    return 0;
}
