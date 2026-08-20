#include "Packet.h"
#include "PartyProtocol.h"
#include "ReceiveBuffer.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

namespace
{
void TestCreatePartyRequest()
{
    const auto payload = dnf::EncodeCreatePartyRequestPayload();
    assert(!payload.empty());

    const auto packetBytes = dnf::EncodePacket(
        dnf::CreatePartyRequest,
        70,
        payload);

    dnf::ReceiveBuffer buffer;
    buffer.Append(packetBytes);

    dnf::Packet request;
    assert(buffer.TryPop(request));
    assert(request.header.type == dnf::CreatePartyRequest);
    dnf::ValidateCreatePartyRequestPayload(request.payload);

    bool threw = false;
    try
    {
        dnf::ValidateCreatePartyRequestPayload({1});
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}

void TestSuccessfulCreatePartyResponse()
{
    const auto payload = dnf::EncodeCreatePartyResponsePayload(
        dnf::CreatePartyResult::Success,
        0x0102030405060708,
        0x1112131415161718);

    assert(!payload.empty());

    const auto response =
        dnf::DecodeCreatePartyResponsePayload(payload);
    assert(response.result == dnf::CreatePartyResult::Success);
    assert(response.partyId == 0x0102030405060708);
    assert(response.leaderSessionId == 0x1112131415161718);
}

void TestFailedCreatePartyResponse()
{
    const auto payload = dnf::EncodeCreatePartyResponsePayload(
        dnf::CreatePartyResult::AlreadyInParty,
        0,
        0);
    const auto response =
        dnf::DecodeCreatePartyResponsePayload(payload);

    assert(response.result == dnf::CreatePartyResult::AlreadyInParty);
    assert(response.partyId == 0);
    assert(response.leaderSessionId == 0);
}

void TestInvalidCreatePartyResponse()
{
    bool threw = false;
    try
    {
        dnf::EncodeCreatePartyResponsePayload(
            dnf::CreatePartyResult::Success,
            0,
            100);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::DecodeCreatePartyResponsePayload(
            dnf::EncodeCreatePartyRequestPayload());
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}

void TestJoinPartyRequest()
{
    const auto payload =
        dnf::EncodeJoinPartyRequestPayload(0x0102030405060708);
    assert(payload.size() == 8);
    assert(payload[0] == 0x01);
    assert(payload[7] == 0x08);
    assert(dnf::DecodeJoinPartyRequestPayload(payload) ==
           0x0102030405060708);

    const auto packetBytes = dnf::EncodePacket(
        dnf::JoinPartyRequest,
        71,
        payload);
    dnf::ReceiveBuffer buffer;
    buffer.Append(packetBytes);

    dnf::Packet request;
    assert(buffer.TryPop(request));
    assert(request.header.type == dnf::JoinPartyRequest);
    assert(dnf::DecodeJoinPartyRequestPayload(request.payload) ==
           0x0102030405060708);
}

void TestJoinPartyResponse()
{
    const auto payload = dnf::EncodeJoinPartyResponsePayload(
        dnf::JoinPartyResult::Success,
        10,
        100);
    const auto response = dnf::DecodeJoinPartyResponsePayload(payload);

    assert(response.result == dnf::JoinPartyResult::Success);
    assert(response.partyId == 10);
    assert(response.leaderSessionId == 100);

    const auto failurePayload = dnf::EncodeJoinPartyResponsePayload(
        dnf::JoinPartyResult::PartyFull,
        0,
        0);
    const auto failure =
        dnf::DecodeJoinPartyResponsePayload(failurePayload);
    assert(failure.result == dnf::JoinPartyResult::PartyFull);
    assert(failure.partyId == 0);
    assert(failure.leaderSessionId == 0);
}

void TestInvalidJoinPartyPayload()
{
    bool threw = false;
    try
    {
        dnf::EncodeJoinPartyRequestPayload(0);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::DecodeJoinPartyRequestPayload({0, 0, 0, 0, 0, 0, 0, 0});
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::EncodeJoinPartyResponsePayload(
            dnf::JoinPartyResult::Success,
            10,
            0);
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);
}

void TestLeavePartyRequest()
{
    const auto packetBytes = dnf::EncodePacket(
        dnf::LeavePartyRequest,
        73,
        {});

    dnf::ReceiveBuffer buffer;
    buffer.Append(packetBytes);

    dnf::Packet request;
    assert(buffer.TryPop(request));
    assert(request.header.type == dnf::LeavePartyRequest);
    dnf::ValidateLeavePartyRequestPayload(request.payload);

    bool threw = false;
    try
    {
        dnf::ValidateLeavePartyRequestPayload({1});
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}

void TestLeavePartyResponse()
{
    const auto successPayload = dnf::EncodeLeavePartyResponsePayload(
        dnf::LeavePartyResult::Success);
    assert(successPayload.size() == 1);
    assert(dnf::DecodeLeavePartyResponsePayload(successPayload) ==
           dnf::LeavePartyResult::Success);

    const auto failurePayload = dnf::EncodeLeavePartyResponsePayload(
        dnf::LeavePartyResult::NotInParty);
    assert(dnf::DecodeLeavePartyResponsePayload(failurePayload) ==
           dnf::LeavePartyResult::NotInParty);

    bool threw = false;
    try
    {
        dnf::DecodeLeavePartyResponsePayload({2});
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}

void TestPartySnapshotRequest()
{
    const auto payload = dnf::EncodePartySnapshotRequestPayload();
    assert(!payload.empty());

    const auto packetBytes = dnf::EncodePacket(
        dnf::PartySnapshotRequest,
        74,
        payload);

    dnf::ReceiveBuffer buffer;
    buffer.Append(packetBytes);

    dnf::Packet request;
    assert(buffer.TryPop(request));
    assert(request.header.type == dnf::PartySnapshotRequest);
    dnf::ValidatePartySnapshotRequestPayload(request.payload);

    bool threw = false;
    try
    {
        dnf::ValidatePartySnapshotRequestPayload({1});
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}

void TestPartySnapshotResponse()
{
    const auto payload = dnf::EncodePartySnapshotResponsePayload(
        dnf::PartySnapshotResult::Success,
        10,
        100,
        {100, 200});

    assert(!payload.empty());

    const auto snapshot =
        dnf::DecodePartySnapshotResponsePayload(payload);
    assert(snapshot.result == dnf::PartySnapshotResult::Success);
    assert(snapshot.partyId == 10);
    assert(snapshot.leaderSessionId == 100);
    assert(snapshot.members.size() == 2);
    assert(snapshot.members[0] == 100);
    assert(snapshot.members[1] == 200);

    const auto failurePayload = dnf::EncodePartySnapshotResponsePayload(
        dnf::PartySnapshotResult::NotInParty,
        0,
        0,
        {});
    const auto failure =
        dnf::DecodePartySnapshotResponsePayload(failurePayload);
    assert(failure.result == dnf::PartySnapshotResult::NotInParty);
    assert(failure.members.empty());
}

void TestInvalidPartySnapshot()
{
    bool threw = false;
    try
    {
        dnf::EncodePartySnapshotResponsePayload(
            dnf::PartySnapshotResult::Success,
            10,
            100,
            {200});
    }
    catch (const std::invalid_argument&)
    {
        threw = true;
    }
    assert(threw);

    threw = false;
    try
    {
        dnf::DecodePartySnapshotResponsePayload(
            dnf::EncodePartySnapshotRequestPayload());
    }
    catch (const std::runtime_error&)
    {
        threw = true;
    }
    assert(threw);
}
} // namespace

int main()
{
    TestCreatePartyRequest();
    TestSuccessfulCreatePartyResponse();
    TestFailedCreatePartyResponse();
    TestInvalidCreatePartyResponse();
    TestJoinPartyRequest();
    TestJoinPartyResponse();
    TestInvalidJoinPartyPayload();
    TestLeavePartyRequest();
    TestLeavePartyResponse();
    TestPartySnapshotRequest();
    TestPartySnapshotResponse();
    TestInvalidPartySnapshot();

    std::cout << "All party protocol tests passed.\n";
    return 0;
}
