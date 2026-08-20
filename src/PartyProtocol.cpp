#include "PartyProtocol.h"
#include "TcpFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <stdexcept>

namespace dnf
{
namespace
{
namespace tcp = Dnf::Protocol::Tcp;

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
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_CreatePartyRequest);
    if (message->payload_as_CreatePartyRequest() == nullptr)
    {
        throw std::runtime_error("Invalid create party request payload");
    }
}

std::vector<std::uint8_t> EncodeCreatePartyRequestPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = tcp::CreateCreatePartyRequest(builder);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_CreatePartyRequest,
        request.Union());
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

    flatbuffers::FlatBufferBuilder builder;
    const auto response = tcp::CreateCreatePartyResponse(
        builder,
        static_cast<tcp::CreatePartyResult>(result),
        partyId,
        leaderSessionId);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_CreatePartyResponse,
        response.Union());
}

CreatePartyResponseData DecodeCreatePartyResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_CreatePartyResponse);
    const auto* response = message->payload_as_CreatePartyResponse();
    if (response == nullptr)
    {
        throw std::runtime_error("Invalid create party response payload");
    }

    const auto result = static_cast<CreatePartyResult>(response->result());
    const PartyId partyId = response->party_id();
    const SessionId leaderSessionId = response->leader_session_id();

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

    flatbuffers::FlatBufferBuilder builder;
    const auto request = tcp::CreateJoinPartyRequest(builder, partyId);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_JoinPartyRequest,
        request.Union());
}

PartyId DecodeJoinPartyRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_JoinPartyRequest);
    const auto* request = message->payload_as_JoinPartyRequest();
    if (request == nullptr)
    {
        throw std::runtime_error("Invalid join party request payload");
    }

    const PartyId partyId = request->party_id();
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

    flatbuffers::FlatBufferBuilder builder;
    const auto response = tcp::CreateJoinPartyResponse(
        builder,
        static_cast<tcp::JoinPartyResult>(result),
        partyId,
        leaderSessionId);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_JoinPartyResponse,
        response.Union());
}

JoinPartyResponseData DecodeJoinPartyResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_JoinPartyResponse);
    const auto* response = message->payload_as_JoinPartyResponse();
    if (response == nullptr)
    {
        throw std::runtime_error("Invalid join party response payload");
    }

    const auto result = static_cast<JoinPartyResult>(response->result());
    const PartyId partyId = response->party_id();
    const SessionId leaderSessionId = response->leader_session_id();

    if (!IsValidJoinPartyResponse(result, partyId, leaderSessionId))
    {
        throw std::runtime_error("Invalid join party response data");
    }

    return {result, partyId, leaderSessionId};
}

void ValidateLeavePartyRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_LeavePartyRequest);
    if (message->payload_as_LeavePartyRequest() == nullptr)
    {
        throw std::runtime_error("Invalid leave party request payload");
    }
}

std::vector<std::uint8_t> EncodeLeavePartyRequestPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = tcp::CreateLeavePartyRequest(builder);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_LeavePartyRequest,
        request.Union());
}

std::vector<std::uint8_t> EncodeLeavePartyResponsePayload(
    LeavePartyResult result)
{
    if (result < LeavePartyResult::Success ||
        result > LeavePartyResult::NotInParty)
    {
        throw std::invalid_argument("Invalid leave party result");
    }

    flatbuffers::FlatBufferBuilder builder;
    const auto response = tcp::CreateLeavePartyResponse(
        builder,
        static_cast<tcp::LeavePartyResult>(result));
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_LeavePartyResponse,
        response.Union());
}

LeavePartyResult DecodeLeavePartyResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_LeavePartyResponse);
    const auto* response = message->payload_as_LeavePartyResponse();
    if (response == nullptr)
    {
        throw std::runtime_error("Invalid leave party response payload");
    }

    const auto result = static_cast<LeavePartyResult>(response->result());
    if (result < LeavePartyResult::Success ||
        result > LeavePartyResult::NotInParty)
    {
        throw std::runtime_error("Invalid leave party response data");
    }

    return result;
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
