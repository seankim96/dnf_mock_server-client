#pragma once

#include "PartyManager.h"

#include <cstdint>
#include <vector>

namespace dnf
{
enum class CreatePartyResult : std::uint8_t
{
    Success,
    AlreadyInParty
};

enum class LeavePartyResult : std::uint8_t
{
    Success,
    NotInParty
};

enum class PartySnapshotResult : std::uint8_t
{
    Success,
    NotInParty
};

struct CreatePartyResponseData
{
    CreatePartyResult result = CreatePartyResult::Success;
    PartyId partyId = 0;
    SessionId leaderSessionId = 0;
};

struct JoinPartyResponseData
{
    JoinPartyResult result = JoinPartyResult::Success;
    PartyId partyId = 0;
    SessionId leaderSessionId = 0;
};

struct PartySnapshotData
{
    PartySnapshotResult result = PartySnapshotResult::NotInParty;
    PartyId partyId = 0;
    SessionId leaderSessionId = 0;
    std::vector<SessionId> members;
};

void ValidateCreatePartyRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeCreatePartyRequestPayload();

std::vector<std::uint8_t> EncodeCreatePartyResponsePayload(
    CreatePartyResult result,
    PartyId partyId,
    SessionId leaderSessionId);

CreatePartyResponseData DecodeCreatePartyResponsePayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeJoinPartyRequestPayload(PartyId partyId);

PartyId DecodeJoinPartyRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeJoinPartyResponsePayload(
    JoinPartyResult result,
    PartyId partyId,
    SessionId leaderSessionId);

JoinPartyResponseData DecodeJoinPartyResponsePayload(
    const std::vector<std::uint8_t>& payload);

void ValidateLeavePartyRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeLeavePartyRequestPayload();

std::vector<std::uint8_t> EncodeLeavePartyResponsePayload(
    LeavePartyResult result);

LeavePartyResult DecodeLeavePartyResponsePayload(
    const std::vector<std::uint8_t>& payload);

void ValidatePartySnapshotRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodePartySnapshotRequestPayload();

std::vector<std::uint8_t> EncodePartySnapshotResponsePayload(
    PartySnapshotResult result,
    PartyId partyId,
    SessionId leaderSessionId,
    const std::vector<SessionId>& members);

PartySnapshotData DecodePartySnapshotResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
