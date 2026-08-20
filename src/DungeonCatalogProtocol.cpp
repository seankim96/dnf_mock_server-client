#include "DungeonCatalogProtocol.h"

#include "PartyManager.h"
#include "TcpFlatBufferCodec.h"

#include <flatbuffers/flatbuffer_builder.h>

#include <stdexcept>
#include <unordered_set>

namespace dnf
{
namespace
{
namespace tcp = Dnf::Protocol::Tcp;

bool IsValidCatalogResult(CatalogResult result)
{
    return result >= CatalogResult::Success &&
           result <= CatalogResult::Unavailable;
}

bool IsValidDungeonEntry(
    DungeonTemplateId templateId,
    const std::string& displayName,
    std::uint8_t recommendedPartySize,
    std::uint8_t maxPartySize)
{
    return templateId != 0 && !displayName.empty() &&
           recommendedPartySize != 0 && maxPartySize != 0 &&
           recommendedPartySize <= maxPartySize &&
           maxPartySize <= MAX_PARTY_MEMBERS;
}
} // namespace

void ValidateDungeonCatalogRequestPayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_DungeonCatalogRequest);
    if (message->payload_as_DungeonCatalogRequest() == nullptr)
    {
        throw std::runtime_error("Invalid dungeon catalog request payload");
    }
}

std::vector<std::uint8_t> EncodeDungeonCatalogRequestPayload()
{
    flatbuffers::FlatBufferBuilder builder;
    const auto request = tcp::CreateDungeonCatalogRequest(builder);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_DungeonCatalogRequest,
        request.Union());
}

std::vector<std::uint8_t> EncodeDungeonCatalogResponsePayload(
    CatalogResult result,
    const std::vector<DungeonTemplate>& dungeons)
{
    if (!IsValidCatalogResult(result) ||
        (result == CatalogResult::Unavailable && !dungeons.empty()))
    {
        throw std::invalid_argument("Invalid dungeon catalog response");
    }

    flatbuffers::FlatBufferBuilder builder;
    std::vector<flatbuffers::Offset<tcp::DungeonCatalogEntry>> entries;
    entries.reserve(dungeons.size());

    for (const DungeonTemplate& dungeon : dungeons)
    {
        if (!IsValidDungeonEntry(
                dungeon.id,
                dungeon.name,
                dungeon.recommendedPartySize,
                dungeon.maxPartySize))
        {
            throw std::invalid_argument("Invalid dungeon catalog entry");
        }

        const auto displayName = builder.CreateString(dungeon.name);
        entries.push_back(tcp::CreateDungeonCatalogEntry(
            builder,
            dungeon.id,
            displayName,
            dungeon.recommendedPartySize,
            dungeon.maxPartySize,
            true));
    }

    const auto dungeonEntries = builder.CreateVector(entries);
    const auto response = tcp::CreateDungeonCatalogResponse(
        builder,
        static_cast<tcp::CatalogResult>(result),
        dungeonEntries);
    return FinishTcpPayload(
        builder,
        tcp::TcpPayload_DungeonCatalogResponse,
        response.Union());
}

DungeonCatalogResponseData DecodeDungeonCatalogResponsePayload(
    const std::vector<std::uint8_t>& payload)
{
    const auto* message = DecodeTcpPayload(
        payload,
        tcp::TcpPayload_DungeonCatalogResponse);
    const auto* response = message->payload_as_DungeonCatalogResponse();
    if (response == nullptr || response->dungeons() == nullptr)
    {
        throw std::runtime_error("Invalid dungeon catalog response payload");
    }

    const auto result = static_cast<CatalogResult>(response->result());
    if (!IsValidCatalogResult(result) ||
        (result == CatalogResult::Unavailable &&
         !response->dungeons()->empty()))
    {
        throw std::runtime_error("Invalid dungeon catalog response data");
    }

    DungeonCatalogResponseData decoded;
    decoded.result = result;
    decoded.dungeons.reserve(response->dungeons()->size());
    std::unordered_set<DungeonTemplateId> templateIds;

    for (const auto* source : *response->dungeons())
    {
        if (source == nullptr || source->display_name() == nullptr ||
            !IsValidDungeonEntry(
                source->dungeon_template_id(),
                source->display_name()->str(),
                source->recommended_party_size(),
                source->max_party_size()) ||
            !templateIds.insert(source->dungeon_template_id()).second)
        {
            throw std::runtime_error("Invalid dungeon catalog entry data");
        }

        decoded.dungeons.push_back({
            source->dungeon_template_id(),
            source->display_name()->str(),
            source->recommended_party_size(),
            source->max_party_size(),
            source->available()});
    }

    return decoded;
}
} // namespace dnf
