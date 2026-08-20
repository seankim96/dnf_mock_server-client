#pragma once

#include "DungeonCatalog.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dnf
{
enum class CatalogResult : std::uint8_t
{
    Success = 0,
    Unavailable = 1
};

struct DungeonCatalogEntryData
{
    DungeonTemplateId templateId = 0;
    std::string displayName;
    std::uint8_t recommendedPartySize = 1;
    std::uint8_t maxPartySize = 4;
    bool available = true;
};

struct DungeonCatalogResponseData
{
    CatalogResult result = CatalogResult::Unavailable;
    std::vector<DungeonCatalogEntryData> dungeons;
};

void ValidateDungeonCatalogRequestPayload(
    const std::vector<std::uint8_t>& payload);

std::vector<std::uint8_t> EncodeDungeonCatalogRequestPayload();

std::vector<std::uint8_t> EncodeDungeonCatalogResponsePayload(
    CatalogResult result,
    const std::vector<DungeonTemplate>& dungeons);

DungeonCatalogResponseData DecodeDungeonCatalogResponsePayload(
    const std::vector<std::uint8_t>& payload);
} // namespace dnf
