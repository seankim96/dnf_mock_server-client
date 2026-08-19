#include "PartyProtocol.h"
#include "TcpFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <stdexcept>

namespace dnf
{
namespace
{
constexpr std::size_t PARTY_ID_SIZE = 8;
constexpr std::size_t PARTY_RESPONSE_SIZE = 17;
namespace tcp = Dnf::Protocol::Tcp;

void AppendUint64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (int shift = 56; shift >= 0; shift -= 8)
    {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint64_t ReadUint64(
    const std::vector<std::uint8_t>& bytes,
    std::size_t offset)
{
    std::uint64_t value = 0;

    for (std::size_t index = 0; index < 8; ++index)
    {
        value = (value << 8) | bytes[offset + index];
    }

    return value;
}

bool IsValidCreatePartyResponse(
    CreatePartyResult result,
    PartyId partyId,
    SessionId leaderSessionId)
{
    if (result < CreatePartyResult::Success ||
        result > CreatePartyResult::AlreadyInParty)
    {
        return false;
    }

    const bool succeeded = result == CreatePartyResult::Success;
    return succeeded == (partyId != 0) &&
           succeeded == (leaderSessionId != 0);
}

bool IsValidJoinPartyResponse(
    JoinPartyResult result,
    PartyId partyId,
    SessionId leaderSessionId)
{
    if (result < JoinPartyResult::Success ||
        result > JoinPartyResult::AlreadyJoined)
    {
        return false;
    }

    const bool succeeded = result == JoinPartyResult::Success;
    return succeeded == (partyId != 0) &&
           succeeded == (leaderSessionId != 0);
}

bool IsValidPartySnapshot(
    PartySnapshotResult result,
    PartyId partyId,
    SessionId leaderSessionId,
    const std::vector<SessionId>& members)
{
    if (result < PartySnapshotResult::Success ||
        result > PartySnapshotResult::NotInParty)
    {
        return false;
    }

    if (result == PartySnapshotResult::NotInParty)
    {
        return partyId == 0 && leaderSessionId == 0 && members.empty();
    }

    if (partyId == 0 || leaderSessionId == 0 || members.empty() ||
        members.size() > MAX_PARTY_MEMBERS)
    {
        return false;
    }

    bool leaderFound = false;
    for (std::size_t index = 0; index < members.size(); ++index)
    {
        if (members[index] == 0)
        {
            return false;
        }

        if (members[index] == leaderSessionId)
        {
            leaderFound = true;
        }

        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (members[previous] == members[index])
            {
                return false;
            }
        }
    }

    return leaderFound;
}
} // namespace

void ValidateCreatePartyRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    if (!payload.empty())
    {
        throw std::runtime_error(
            "Create party request payload must be empty");
    }
}

std::vector<std::uint8_t> EncodeCreatePartyResponsePayload(
    CreatePartyResult result,
    PartyId partyId,
    SessionId leaderSessionId)
{
    if (!IsValidCreatePartyResponse(result, partyId, leaderSessionId))
    {
        throw std::invalid_argument("Invalid create party response");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(PARTY_RESPONSE_SIZE);
    payload.push_back(static_cast<std::uint8_t>(result));
    AppendUint64(payload, partyId);
    AppendUint64(payload, leaderSessionId);
    return payload;
}

CreatePartyResponseData DecodeCreatePartyResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != PARTY_RESPONSE_SIZE)
    {
        throw std::runtime_error(
            "Invalid create party response payload size");
    }

    const auto result = static_cast<CreatePartyResult>(payload[0]);
    const PartyId partyId = ReadUint64(payload, 1);
    const SessionId leaderSessionId = ReadUint64(payload, 9);

    if (!IsValidCreatePartyResponse(result, partyId, leaderSessionId))
    {
        throw std::runtime_error("Invalid create party response data");
    }

    return {result, partyId, leaderSessionId};
}

std::vector<std::uint8_t> EncodeJoinPartyRequestPayload(PartyId partyId)
{
    if (partyId == 0)
    {
        throw std::invalid_argument("Party ID must not be zero");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(PARTY_ID_SIZE);
    AppendUint64(payload, partyId);
    return payload;
}

PartyId DecodeJoinPartyRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != PARTY_ID_SIZE)
    {
        throw std::runtime_error("Invalid join party request payload size");
    }

    const PartyId partyId = ReadUint64(payload, 0);
    if (partyId == 0)
    {
        throw std::runtime_error("Invalid join party request data");
    }

    return partyId;
}

std::vector<std::uint8_t> EncodeJoinPartyResponsePayload(
    JoinPartyResult result,
    PartyId partyId,
    SessionId leaderSessionId)
{
    if (!IsValidJoinPartyResponse(result, partyId, leaderSessionId))
    {
        throw std::invalid_argument("Invalid join party response");
    }

    std::vector<std::uint8_t> payload;
    payload.reserve(PARTY_RESPONSE_SIZE);
    payload.push_back(static_cast<std::uint8_t>(result));
    AppendUint64(payload, partyId);
    AppendUint64(payload, leaderSessionId);
    return payload;
}

JoinPartyResponseData DecodeJoinPartyResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != PARTY_RESPONSE_SIZE)
    {
        throw std::runtime_error(
            "Invalid join party response payload size");
    }

    const auto result = static_cast<JoinPartyResult>(payload[0]);
    const PartyId partyId = ReadUint64(payload, 1);
    const SessionId leaderSessionId = ReadUint64(payload, 9);

    if (!IsValidJoinPartyResponse(result, partyId, leaderSessionId))
    {
        throw std::runtime_error("Invalid join party response data");
    }

    return {result, partyId, leaderSessionId};
}

void ValidateLeavePartyRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    if (!payload.empty())
    {
        throw std::runtime_error(
            "Leave party request payload must be empty");
    }
}

std::vector<std::uint8_t> EncodeLeavePartyResponsePayload(
    LeavePartyResult result)
{
    if (result < LeavePartyResult::Success ||
        result > LeavePartyResult::NotInParty)
    {
        throw std::invalid_argument("Invalid leave party result");
    }

    return {static_cast<std::uint8_t>(result)};
}

LeavePartyResult DecodeLeavePartyResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() != 1 ||
        payload[0] > static_cast<std::uint8_t>(LeavePartyResult::NotInParty))
    {
        throw std::runtime_error("Invalid leave party response payload");
    }

    return static_cast<LeavePartyResult>(payload[0]);
}

void ValidatePartySnapshotRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_PartySnapshotRequest);
    if (message->payload_as_PartySnapshotRequest() == nullptr)
    {
        throw std::runtime_error("Invalid party snapshot request payload");
    }
}

std::vector<std::uint8_t> EncodePartySnapshotRequestPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = tcp::CreatePartySnapshotRequest(builder);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_PartySnapshotRequest,
        request.Union());
}

std::vector<std::uint8_t> EncodePartySnapshotResponsePayload(
    PartySnapshotResult result,
    PartyId partyId,
    SessionId leaderSessionId,
    const std::vector<SessionId>& members)
{
    if (!IsValidPartySnapshot(
            result,
            partyId,
            leaderSessionId,
            members))
    {
        throw std::invalid_argument("Invalid party snapshot response");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto memberIds = builder.CreateVector(members);
    const auto response = tcp::CreatePartySnapshotResponse(
        builder,
        static_cast<tcp::PartySnapshotResult>(result),
        partyId,
        leaderSessionId,
        memberIds);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_PartySnapshotResponse,
        response.Union());
}

PartySnapshotData DecodePartySnapshotResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_PartySnapshotResponse);
    const auto* response = message->payload_as_PartySnapshotResponse();
    if (response == nullptr || response->member_session_ids() == nullptr)
    {
        throw std::runtime_error("Invalid party snapshot response payload");
    }

    const auto result =
        static_cast<PartySnapshotResult>(response->result());
    const PartyId partyId = response->party_id();
    const SessionId leaderSessionId = response->leader_session_id();
    std::vector<SessionId> members;
    members.assign(
        response->member_session_ids()->begin(),
        response->member_session_ids()->end());

    if (!IsValidPartySnapshot(
            result,
            partyId,
            leaderSessionId,
            members))
    {
        throw std::runtime_error("Invalid party snapshot response data");
    }

    return {result, partyId, leaderSessionId, members};
}
} // namespace dnf
